/*
 * This file has a C ABI (see emoji_renderer.h's extern "C" guard) but is
 * compiled as C++ (CMakeLists.txt forces LANGUAGE CXX on this one
 * translation unit; every other .c file in the project stays C11).
 *
 * Why: in the current Windows SDK, neither dwrite.h nor d2d1.h has a
 * working C-language interface path.
 *   - dwrite.h unconditionally declares e.g.
 *     "interface IDWriteFactory : public IUnknown" with no
 *     __cplusplus/CINTERFACE guard at all, so a plain C compiler can't
 *     parse it (this was the original reported build failure).
 *   - d2d1.h does have a D2D_USE_C_DEFINITIONS toggle, but in this SDK
 *     that mode only forward-declares opaque interface names (zero
 *     methods, zero Vtbl structs) -- there is no COBJMACROS-style
 *     "ID2D1Xxx_Method(this, ...)" form to call either, despite older
 *     SDKs having supported that pattern.
 * So every COM call below -- Direct2D and DirectWrite alike -- uses real
 * C++ method syntax (obj->Method(...)) instead of C-style macros.
 */
#include "emoji_renderer.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wchar.h>
#include <strsafe.h>

/* Added to D2D1_DRAW_TEXT_OPTIONS by color-font support. Some SDK/header
 * combinations hide the enum member behind target-version macros, so provide
 * the documented numeric value as a source-compatible fallback. */
#ifndef D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
#define D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT ((D2D1_DRAW_TEXT_OPTIONS)0x00000004u)
#endif

/* ID2D1RenderTarget::DrawText builds a throwaway IDWriteTextLayout (full
 * text analysis, font-fallback resolution and glyph shaping) on every call.
 * Scrolling or paging through an emoji-only page redraws every visible cell
 * every time the same set of symbols keeps getting re-shaped from scratch,
 * which is the main cost behind "scrolling/paging feels slow on emoji
 * pages". Caching one IDWriteTextLayout per distinct symbol text and
 * reusing it via DrawTextLayout skips that repeated shaping work; only
 * SetMaxWidth/SetMaxHeight (cheap -- they just adjust the layout box, they
 * don't re-shape) are refreshed per draw, so a symbol cached from a
 * differently-sized context (e.g. the recent-use strip vs. the main grid)
 * still lays out correctly. Capacity is a fixed power-of-two open-addressed
 * table sized comfortably above the ~3900 actual emoji symbols in the
 * catalog, so a full background warm-up (see
 * ys_emoji_renderer_warm_cache_async below) does not fill it; if it ever
 * does fill up anyway, the whole cache is dropped and rebuilt rather than
 * tracking per-entry recency, which keeps the eviction logic simple and
 * correct at the cost of an occasional one-time re-shape burst.
 *
 * The cache is shared between the UI thread (draws, via
 * ys_emoji_renderer_draw) and an optional background warm-up thread, so all
 * access to layout_cache/layout_cache_used must hold cache_lock. Only the
 * UI thread's own insert path (in ys_emoji_renderer_draw) is allowed to
 * evict/clear the cache: eviction Release()s every cached layout, and the
 * UI thread may be mid-DrawTextLayout on a pointer it fetched outside the
 * lock, so a release racing with that use would be a use-after-free. The
 * background thread only ever inserts (skipping a symbol instead of
 * evicting if the table is briefly full), which never touches an object
 * anyone else might currently be using. */
#define YS_EMOJI_LAYOUT_CACHE_CAPACITY 8192u
#define YS_EMOJI_LAYOUT_CACHE_TEXT_CAPACITY 36u
/* Warm-up is split across at most this many low-priority worker threads
 * (see ys_emoji_renderer_warm_cache_async) so the whole background pass
 * finishes sooner in wall-clock time, capped to avoid over-subscribing on
 * very high-core-count machines for what is purely opportunistic work. */
#define YS_EMOJI_WARMUP_MAX_THREADS 4u

typedef struct YSEmojiLayoutCacheSlot {
    WCHAR text[YS_EMOJI_LAYOUT_CACHE_TEXT_CAPACITY];
    IDWriteTextLayout *layout;
} YSEmojiLayoutCacheSlot;

struct YSEmojiRenderer {
    ID2D1Factory *d2d_factory;
    IDWriteFactory *dwrite_factory;
    ID2D1DCRenderTarget *dc_target;
    ID2D1SolidColorBrush *brush;
    IDWriteTextFormat *text_format;
    YSEmojiLayoutCacheSlot *layout_cache;
    UINT32 layout_cache_used;
    CRITICAL_SECTION cache_lock;
    HANDLE warmup_threads[YS_EMOJI_WARMUP_MAX_THREADS];
    UINT32 warmup_thread_count;
    volatile LONG warmup_cancel;
    BOOL drawing;
    BOOL available;
};

static UINT32 ys_emoji_text_hash(const WCHAR *text) {
    UINT32 hash = 2166136261u;
    while (*text) {
        hash ^= (UINT16)*text++;
        hash *= 16777619u;
    }
    return hash;
}

/* renderer->cache_lock must be held by the caller for all three functions
 * below -- they do no locking of their own so callers can hold the lock
 * across a find-then-maybe-insert sequence without deadlocking. */

static void ys_emoji_layout_cache_clear(YSEmojiRenderer *renderer) {
    UINT32 i;
    if (!renderer || !renderer->layout_cache) return;
    for (i = 0; i < YS_EMOJI_LAYOUT_CACHE_CAPACITY; ++i) {
        if (renderer->layout_cache[i].layout) {
            renderer->layout_cache[i].layout->Release();
            renderer->layout_cache[i].layout = NULL;
            renderer->layout_cache[i].text[0] = 0;
        }
    }
    renderer->layout_cache_used = 0;
}

static IDWriteTextLayout *ys_emoji_layout_cache_find(YSEmojiRenderer *renderer, const WCHAR *text) {
    UINT32 mask = YS_EMOJI_LAYOUT_CACHE_CAPACITY - 1u;
    UINT32 slot = ys_emoji_text_hash(text) & mask;
    UINT32 start = slot;
    if (!renderer->layout_cache) return NULL;
    do {
        if (!renderer->layout_cache[slot].layout) return NULL;
        if (wcscmp(renderer->layout_cache[slot].text, text) == 0) return renderer->layout_cache[slot].layout;
        slot = (slot + 1u) & mask;
    } while (slot != start);
    return NULL;
}

static BOOL ys_emoji_layout_cache_insert(YSEmojiRenderer *renderer, const WCHAR *text, IDWriteTextLayout *layout) {
    UINT32 mask = YS_EMOJI_LAYOUT_CACHE_CAPACITY - 1u;
    UINT32 slot = ys_emoji_text_hash(text) & mask;
    UINT32 start = slot;
    if (!renderer->layout_cache) return FALSE;
    do {
        if (!renderer->layout_cache[slot].layout) {
            StringCchCopyW(renderer->layout_cache[slot].text, YS_EMOJI_LAYOUT_CACHE_TEXT_CAPACITY, text);
            renderer->layout_cache[slot].layout = layout;
            ++renderer->layout_cache_used;
            return TRUE;
        }
        slot = (slot + 1u) & mask;
    } while (slot != start);
    return FALSE;
}

static void ys_emoji_renderer_release_target(YSEmojiRenderer *renderer) {
    if (!renderer) return;
    if (renderer->brush) {
        renderer->brush->Release();
        renderer->brush = NULL;
    }
    if (renderer->dc_target) {
        renderer->dc_target->Release();
        renderer->dc_target = NULL;
    }
    renderer->drawing = FALSE;
}

static BOOL ys_emoji_renderer_create_target(YSEmojiRenderer *renderer) {
    D2D1_RENDER_TARGET_PROPERTIES properties;
    D2D1_COLOR_F black;
    HRESULT result;
    if (!renderer || !renderer->d2d_factory) return FALSE;
    if (renderer->dc_target && renderer->brush) return TRUE;

    ZeroMemory(&properties, sizeof(properties));
    properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    /* YeSymbol's layout rectangles are expressed in physical client pixels.
     * Using 96 DPI keeps one Direct2D DIP equal to one layout pixel. */
    properties.dpiX = 96.0f;
    properties.dpiY = 96.0f;
    properties.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    properties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    result = renderer->d2d_factory->CreateDCRenderTarget(&properties, &renderer->dc_target);
    if (FAILED(result) || !renderer->dc_target) {
        ys_emoji_renderer_release_target(renderer);
        return FALSE;
    }

    black.r = 0.0f;
    black.g = 0.0f;
    black.b = 0.0f;
    black.a = 1.0f;
    result = renderer->dc_target->CreateSolidColorBrush(&black, NULL, &renderer->brush);
    if (FAILED(result) || !renderer->brush) {
        ys_emoji_renderer_release_target(renderer);
        return FALSE;
    }
    return TRUE;
}

YSEmojiRenderer *ys_emoji_renderer_create(float font_size_pixels) {
    YSEmojiRenderer *renderer;
    HRESULT result;
    renderer = (YSEmojiRenderer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*renderer));
    if (!renderer) return NULL;
    /* Initialized before anything else so ys_emoji_renderer_destroy can
     * unconditionally delete it on any later failure path below. */
    InitializeCriticalSection(&renderer->cache_lock);

    result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               IID_ID2D1Factory, NULL,
                               (void **)&renderer->d2d_factory);
    if (FAILED(result) || !renderer->d2d_factory) goto failed;

    /* IID_IDWriteFactory is not declared anywhere in the SDK headers (only
     * IID_ID2D1Factory is); __uuidof reads the GUID that DWRITE_DECLARE_INTERFACE
     * attached to the type instead. */
    result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                 __uuidof(IDWriteFactory),
                                 (IUnknown **)&renderer->dwrite_factory);
    if (FAILED(result) || !renderer->dwrite_factory) goto failed;

    result = renderer->dwrite_factory->CreateTextFormat(
        L"Segoe UI Emoji",
        NULL,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        font_size_pixels > 0.0f ? font_size_pixels : 24.0f,
        L"zh-CN",
        &renderer->text_format);
    if (FAILED(result) || !renderer->text_format) goto failed;

    renderer->text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    renderer->text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    renderer->text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    renderer->layout_cache = (YSEmojiLayoutCacheSlot *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(YSEmojiLayoutCacheSlot) * YS_EMOJI_LAYOUT_CACHE_CAPACITY);
    if (!renderer->layout_cache) goto failed;

    if (!ys_emoji_renderer_create_target(renderer)) goto failed;
    renderer->available = TRUE;
    return renderer;

failed:
    ys_emoji_renderer_destroy(renderer);
    return NULL;
}

void ys_emoji_renderer_destroy(YSEmojiRenderer *renderer) {
    if (!renderer) return;
    if (renderer->warmup_thread_count) {
        UINT32 i;
        /* Each worker's loop checks this every symbol, so all of them
         * notice and exit promptly; wait for every one before touching
         * anything they might still be reading (dwrite_factory,
         * text_format, the cache). */
        InterlockedExchange(&renderer->warmup_cancel, 1);
        WaitForMultipleObjects(renderer->warmup_thread_count, renderer->warmup_threads, TRUE, INFINITE);
        for (i = 0; i < renderer->warmup_thread_count; ++i) CloseHandle(renderer->warmup_threads[i]);
        renderer->warmup_thread_count = 0;
    }
    ys_emoji_renderer_release_target(renderer);
    ys_emoji_layout_cache_clear(renderer);
    if (renderer->layout_cache) HeapFree(GetProcessHeap(), 0, renderer->layout_cache);
    if (renderer->text_format) renderer->text_format->Release();
    if (renderer->dwrite_factory) renderer->dwrite_factory->Release();
    if (renderer->d2d_factory) renderer->d2d_factory->Release();
    DeleteCriticalSection(&renderer->cache_lock);
    HeapFree(GetProcessHeap(), 0, renderer);
}

BOOL ys_emoji_renderer_is_available(const YSEmojiRenderer *renderer) {
    return renderer && renderer->available && renderer->text_format;
}

BOOL ys_emoji_renderer_begin(YSEmojiRenderer *renderer, HDC dc, const RECT *bounds) {
    D2D1_MATRIX_3X2_F identity;
    HRESULT result;
    if (!ys_emoji_renderer_is_available(renderer) || !dc || !bounds ||
        bounds->right <= bounds->left || bounds->bottom <= bounds->top) return FALSE;
    if (renderer->drawing) return TRUE;
    if (!ys_emoji_renderer_create_target(renderer)) return FALSE;

    result = renderer->dc_target->BindDC(dc, bounds);
    if (FAILED(result)) {
        ys_emoji_renderer_release_target(renderer);
        return FALSE;
    }

    identity._11 = 1.0f;
    identity._12 = 0.0f;
    identity._21 = 0.0f;
    identity._22 = 1.0f;
    identity._31 = 0.0f;
    identity._32 = 0.0f;
    renderer->dc_target->SetTransform(&identity);
    renderer->dc_target->BeginDraw();
    renderer->drawing = TRUE;
    return TRUE;
}

BOOL ys_emoji_renderer_draw(YSEmojiRenderer *renderer, const WCHAR *text,
                            const RECT *layout_rect, COLORREF fallback_color) {
    D2D1_RECT_F draw_rect;
    D2D1_COLOR_F color;
    D2D1_DRAW_TEXT_OPTIONS options;
    D2D1_POINT_2F origin;
    IDWriteTextLayout *layout;
    FLOAT width, height;
    HRESULT result;
    BOOL owned_layout = FALSE;
    if (!renderer || !renderer->drawing || !text || !text[0] || !layout_rect) return FALSE;

    color.r = (float)GetRValue(fallback_color) / 255.0f;
    color.g = (float)GetGValue(fallback_color) / 255.0f;
    color.b = (float)GetBValue(fallback_color) / 255.0f;
    color.a = 1.0f;
    renderer->brush->SetColor(&color);

    draw_rect.left = (FLOAT)layout_rect->left;
    draw_rect.top = (FLOAT)layout_rect->top;
    draw_rect.right = (FLOAT)layout_rect->right;
    draw_rect.bottom = (FLOAT)layout_rect->bottom;
    width = draw_rect.right - draw_rect.left;
    height = draw_rect.bottom - draw_rect.top;
    if (width <= 0.0f || height <= 0.0f) return FALSE;

    EnterCriticalSection(&renderer->cache_lock);
    layout = ys_emoji_layout_cache_find(renderer, text);
    LeaveCriticalSection(&renderer->cache_lock);

    if (layout) {
        /* Cheap: only adjusts the layout box, does not re-run text
         * analysis/shaping, so a symbol cached from a different-sized
         * context (e.g. the recent-use strip, or the background warm-up's
         * default cell size) still lays out correctly here. */
        layout->SetMaxWidth(width);
        layout->SetMaxHeight(height);
    } else {
        IDWriteTextLayout *existing;
        /* CreateTextLayout runs without the lock held -- it can run
         * concurrently with the warm-up thread doing the same for other
         * symbols (DirectWrite's shared factory is documented safe for
         * that), and it keeps this possibly-slow, first-time shaping work
         * from blocking anyone else's cache access. */
        result = renderer->dwrite_factory->CreateTextLayout(text, (UINT32)wcslen(text),
                                                              renderer->text_format,
                                                              width, height, &layout);
        if (FAILED(result) || !layout) return FALSE;

        EnterCriticalSection(&renderer->cache_lock);
        existing = ys_emoji_layout_cache_find(renderer, text);
        if (existing) {
            /* The warm-up thread cached this same text while we were
             * creating our own copy above: drop ours and use the shared
             * entry instead of keeping two layouts for the same symbol. */
            LeaveCriticalSection(&renderer->cache_lock);
            layout->Release();
            layout = existing;
            layout->SetMaxWidth(width);
            layout->SetMaxHeight(height);
        } else {
            if (renderer->layout_cache_used >= (YS_EMOJI_LAYOUT_CACHE_CAPACITY * 3u) / 4u) {
                ys_emoji_layout_cache_clear(renderer);
            }
            /* Insert failing here (table still full right after a clear)
             * would mean layout_cache_used is far larger than the actual
             * emoji vocabulary -- not expected, but stay correct: draw this
             * one time and release it ourselves instead of leaking it. */
            if (!ys_emoji_layout_cache_insert(renderer, text, layout)) owned_layout = TRUE;
            LeaveCriticalSection(&renderer->cache_lock);
        }
    }

    origin.x = draw_rect.left;
    origin.y = draw_rect.top;
    options = (D2D1_DRAW_TEXT_OPTIONS)(D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    renderer->dc_target->DrawTextLayout(origin, layout, renderer->brush, options);
    if (owned_layout) layout->Release();
    return TRUE;
}

/* Shared by every worker thread spawned for one ys_emoji_renderer_warm_cache_async
 * call. `texts` is caller-ordered (see ui.c: category display order, most
 * likely to be opened first, comes first) and split into contiguous
 * per-thread slices below -- each worker stays priority-ordered within its
 * own slice, and running several slices concurrently finishes the whole
 * pass sooner without disturbing that ordering. active_workers starts at
 * the number of slices actually handed to a running thread; whichever
 * worker (or, if every CreateThread call failed, the spawning call itself)
 * decrements it to zero owns freeing `texts` and this job struct. */
typedef struct YSEmojiWarmupJob {
    YSEmojiRenderer *renderer;
    const WCHAR **texts;
    FLOAT cell_width;
    FLOAT cell_height;
    volatile LONG active_workers;
} YSEmojiWarmupJob;

typedef struct YSEmojiWarmupSlice {
    YSEmojiWarmupJob *job;
    size_t start;
    size_t end;
} YSEmojiWarmupSlice;

static void ys_emoji_warmup_job_release(YSEmojiWarmupJob *job) {
    if (InterlockedDecrement(&job->active_workers) == 0) {
        HeapFree(GetProcessHeap(), 0, job->texts);
        HeapFree(GetProcessHeap(), 0, job);
    }
}

/* Ensures `text` has a cached layout, creating one via CreateTextLayout if
 * needed. Used by both the synchronous and background warm-up paths, which
 * only need the cache populated (unlike ys_emoji_renderer_draw, which also
 * needs the layout pointer back to draw with immediately).
 *
 * allow_evict must only be TRUE for calls made from the UI thread: eviction
 * Release()s every cached layout, and if a background worker triggered it
 * while the UI thread was mid-DrawTextLayout on a pointer fetched outside
 * the lock (see ys_emoji_renderer_draw), that would be a use-after-free.
 * The UI-thread-only synchronous warm-up path is safe to evict from
 * because it runs before the window is shown, i.e. before anything could
 * possibly be mid-draw yet. */
static void ys_emoji_renderer_ensure_cached(YSEmojiRenderer *renderer, const WCHAR *text,
                                            FLOAT width, FLOAT height, BOOL allow_evict) {
    IDWriteTextLayout *layout;
    IDWriteTextLayout *existing;
    HRESULT result;
    if (!text || !text[0]) return;

    EnterCriticalSection(&renderer->cache_lock);
    existing = ys_emoji_layout_cache_find(renderer, text);
    LeaveCriticalSection(&renderer->cache_lock);
    if (existing) return;

    result = renderer->dwrite_factory->CreateTextLayout(text, (UINT32)wcslen(text),
                                                         renderer->text_format, width, height, &layout);
    if (FAILED(result) || !layout) return;

    EnterCriticalSection(&renderer->cache_lock);
    existing = ys_emoji_layout_cache_find(renderer, text);
    if (existing) {
        LeaveCriticalSection(&renderer->cache_lock);
        layout->Release();
        return;
    }
    if (allow_evict && renderer->layout_cache_used >= (YS_EMOJI_LAYOUT_CACHE_CAPACITY * 3u) / 4u) {
        ys_emoji_layout_cache_clear(renderer);
    }
    if (!ys_emoji_layout_cache_insert(renderer, text, layout)) layout->Release();
    LeaveCriticalSection(&renderer->cache_lock);
}

static DWORD WINAPI ys_emoji_renderer_warmup_proc(LPVOID param) {
    YSEmojiWarmupSlice *slice = (YSEmojiWarmupSlice *)param;
    YSEmojiWarmupJob *job = slice->job;
    YSEmojiRenderer *renderer = job->renderer;
    size_t i;
    for (i = slice->start; i < slice->end; ++i) {
        if (renderer->warmup_cancel) break;
        ys_emoji_renderer_ensure_cached(renderer, job->texts[i], job->cell_width, job->cell_height, FALSE);
    }
    HeapFree(GetProcessHeap(), 0, slice);
    ys_emoji_warmup_job_release(job);
    return 0;
}

void ys_emoji_renderer_warm_cache_sync(YSEmojiRenderer *renderer,
                                       const WCHAR *const *texts, size_t text_count,
                                       int cell_width, int cell_height) {
    size_t i;
    if (!ys_emoji_renderer_is_available(renderer) || !texts || cell_width <= 0 || cell_height <= 0) return;
    for (i = 0; i < text_count; ++i) {
        ys_emoji_renderer_ensure_cached(renderer, texts[i], (FLOAT)cell_width, (FLOAT)cell_height, TRUE);
    }
}

BOOL ys_emoji_renderer_warm_cache_async(YSEmojiRenderer *renderer,
                                        const WCHAR **texts, size_t text_count,
                                        int cell_width, int cell_height) {
    YSEmojiWarmupJob *job;
    SYSTEM_INFO system_info;
    UINT32 thread_count, i;
    size_t per_thread, start;
    if (!ys_emoji_renderer_is_available(renderer) || !texts || !text_count ||
        cell_width <= 0 || cell_height <= 0 || renderer->warmup_thread_count) {
        if (texts) HeapFree(GetProcessHeap(), 0, texts);
        return FALSE;
    }
    job = (YSEmojiWarmupJob *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*job));
    if (!job) {
        HeapFree(GetProcessHeap(), 0, texts);
        return FALSE;
    }
    job->renderer = renderer;
    job->texts = texts;
    job->cell_width = (FLOAT)cell_width;
    job->cell_height = (FLOAT)cell_height;

    GetSystemInfo(&system_info);
    thread_count = system_info.dwNumberOfProcessors > 1u
                       ? min(YS_EMOJI_WARMUP_MAX_THREADS, system_info.dwNumberOfProcessors - 1u)
                       : 1u;
    if ((size_t)thread_count > text_count) thread_count = (UINT32)text_count;
    job->active_workers = (LONG)thread_count;

    per_thread = text_count / thread_count;
    start = 0;
    for (i = 0; i < thread_count; ++i) {
        YSEmojiWarmupSlice *slice;
        size_t end = (i + 1u == thread_count) ? text_count : start + per_thread;
        HANDLE thread;

        slice = (YSEmojiWarmupSlice *)HeapAlloc(GetProcessHeap(), 0, sizeof(*slice));
        if (!slice) {
            ys_emoji_warmup_job_release(job);
            start = end;
            continue;
        }
        slice->job = job;
        slice->start = start;
        slice->end = end;

        thread = CreateThread(NULL, 0, ys_emoji_renderer_warmup_proc, slice, 0, NULL);
        if (!thread) {
            HeapFree(GetProcessHeap(), 0, slice);
            ys_emoji_warmup_job_release(job);
            start = end;
            continue;
        }
        /* Normal priority, not lowest: THREAD_PRIORITY_LOWEST can leave this
         * thread starved for CPU time for a long while whenever anything
         * else on the system is even mildly busy, which made warm-up
         * effectively never finish in practice. This work is still bounded
         * and short-lived (each iteration is a brief CreateTextLayout call
         * plus a short lock), and one core is deliberately left unused (see
         * thread_count below) for the UI thread, so running at normal
         * priority should not cause noticeable UI jank while actually
         * getting scheduled promptly. */
        SetThreadPriority(thread, THREAD_PRIORITY_NORMAL);
        renderer->warmup_threads[renderer->warmup_thread_count++] = thread;
        start = end;
    }
    return renderer->warmup_thread_count > 0;
}

BOOL ys_emoji_renderer_end(YSEmojiRenderer *renderer) {
    HRESULT result;
    if (!renderer || !renderer->drawing) return FALSE;
    result = renderer->dc_target->EndDraw(NULL, NULL);
    renderer->drawing = FALSE;
    if (FAILED(result)) {
        /* A recreated target is cheap and gives the next paint a clean retry.
         * GDI remains the caller's fallback for the current frame. */
        ys_emoji_renderer_release_target(renderer);
        return FALSE;
    }
    return TRUE;
}
