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
#include "emoji_image_data.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wchar.h>
#include <strsafe.h>

/* Added to D2D1_DRAW_TEXT_OPTIONS by color-font support. Some SDK/header
 * combinations hide the enum member behind target-version macros, so provide
 * the documented numeric value as a source-compatible fallback. */
#ifndef D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
#define D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT ((D2D1_DRAW_TEXT_OPTIONS)0x00000004u)
#endif

/* --- Two caches, two different costs being avoided ---
 *
 * IDWriteTextLayout cache: ID2D1RenderTarget::DrawText builds a throwaway
 * layout (text analysis, font-fallback resolution, glyph selection) on
 * every call. Caching one per distinct symbol text and reusing it via
 * DrawTextLayout skips that repeated analysis.
 *
 * Bitmap cache (the actual fix for "first click into an emoji category is
 * slow, second click is fast"): a cached IDWriteTextLayout still has to be
 * rasterized -- the color font's glyph outlines/COLR-CPAL data turned into
 * actual pixels -- on every single DrawTextLayout call. That rasterization
 * cost, not the layout analysis, is what made warming only the layout
 * cache ineffective. This renderer bakes each distinct symbol to a small
 * 32bpp premultiplied-alpha bitmap on demand, and every subsequent draw is a plain GDI
 * AlphaBlend of that bitmap -- no DirectWrite/Direct2D call at all, so the
 * expensive part only ever happens once per symbol, whenever that happens
 * to be until the bounded cache evicts its least-recently-used entry.
 *
 * The layout cache is an open-addressed table. The bitmap cache is a small
 * linear LRU: 384 entries cover several visible screens while keeping GDI
 * handles and pixel memory bounded regardless of catalog size.
 *
 * --- Threading rules (all three matter; violating any one of them
 * produced hangs/crashes during development) ---
 *
 * 1. Cache storage access holds cache_lock.
 *    Only the UI thread is allowed to evict/clear a cache: eviction
 *    releases every cached entry, and the UI thread may be
 *    mid-AlphaBlend/mid-DrawTextLayout on a pointer it fetched outside the
 *    lock, so a release racing with that use would be a use-after-free.
 *    Background workers only ever insert (skipping a symbol instead of
 *    evicting if a table is briefly full), which never touches an entry
 *    anyone else might currently be using.
 *
 * 2. bake_target has thread *affinity*, not just a concurrency
 *    requirement. It is created from bake_factory, which must be
 *    D2D1_FACTORY_TYPE_MULTI_THREADED -- resources from a
 *    SINGLE_THREADED factory may only ever be called from the one thread
 *    that created them, and no amount of external locking makes a
 *    cross-thread call legal. (The main dc_target pipeline keeps its own
 *    SINGLE_THREADED d2d_factory precisely because it is UI-thread-only.)
 *    bake_lock is still needed on top of that, since one render target
 *    cannot service two BeginDraw/EndDraw sequences at once: baking is
 *    serialized through it, while the cheap "is this already cached?"
 *    check stays parallel.
 *
 * 3. Cached IDWriteTextLayout objects are mutable shared state -- the draw
 *    path calls SetMaxWidth/SetMaxHeight on them before drawing. They are
 *    therefore UI-thread-only, and the bake path deliberately creates its
 *    own private, short-lived layout instead of borrowing a cached one
 *    (see ys_emoji_renderer_ensure_baked). */
#define YS_EMOJI_LAYOUT_CACHE_CAPACITY 8192u
#define YS_EMOJI_LAYOUT_CACHE_TEXT_CAPACITY 36u
#define YS_EMOJI_BITMAP_CACHE_CAPACITY 384u
/* Keep emoji artwork visually consistent in the main grid and recent-use
 * strip. The backing cell bitmap remains cell-sized for cache compatibility;
 * the image itself is decoded and blitted as a centred 36x36 square. */
#define YS_EMOJI_IMAGE_SIDE 36
/* Symbols that cannot use the cached-bitmap path are queued here and drawn
 * together at ys_emoji_renderer_end time. See the comment on
 * ys_emoji_renderer_begin for why they cannot simply be drawn inline. */
#define YS_EMOJI_FALLBACK_QUEUE_CAPACITY 256u

typedef struct YSEmojiLayoutCacheSlot {
    WCHAR text[YS_EMOJI_LAYOUT_CACHE_TEXT_CAPACITY];
    IDWriteTextLayout *layout;
} YSEmojiLayoutCacheSlot;

typedef struct YSEmojiBitmapCacheSlot {
    WCHAR text[YS_EMOJI_LAYOUT_CACHE_TEXT_CAPACITY];
    HBITMAP bitmap;
    ULONGLONG last_used;
} YSEmojiBitmapCacheSlot;

/* Everything one thread needs to bake a bitmap. The renderer owns one of
 * these for the UI thread (guarded by bake_lock, since WM_PAINT and any
 * other UI-thread caller share it); each warm-up worker builds its own on
 * its stack, so workers bake genuinely in parallel instead of queueing on
 * a single shared render target. All contexts come from the same
 * MULTI_THREADED bake_factory, which is what makes per-thread use legal. */
typedef struct YSEmojiFallbackItem {
    WCHAR text[YS_EMOJI_LAYOUT_CACHE_TEXT_CAPACITY];
    RECT rect;
    COLORREF color;
} YSEmojiFallbackItem;

typedef struct YSEmojiBakeContext {
    ID2D1DCRenderTarget *target;
    ID2D1SolidColorBrush *brush;
    /* Used to decode the embedded Twemoji PNGs (see emoji_image_data.h).
     * WIC needs COM initialized on the calling thread, and each bake context
     * belongs to exactly one thread, so the context owns both. */
    IWICImagingFactory *wic;
    BOOL com_initialized;
    HDC dc;
    HBITMAP dc_stock;
    /* A single scratch DIB, selected into `dc` and BindDC'd once at init.
     * Every bake renders into this same surface and then copies the pixels
     * out into the bitmap it returns. Re-binding per bake (the obvious
     * implementation) turned out to dominate bake cost, since each BindDC
     * revalidates the target's backing surface. */
    HBITMAP scratch;
    void *scratch_bits;
} YSEmojiBakeContext;

struct YSEmojiRenderer {
    ID2D1Factory *d2d_factory;
    IDWriteFactory *dwrite_factory;
    ID2D1DCRenderTarget *dc_target;
    ID2D1SolidColorBrush *brush;
    IDWriteTextFormat *text_format;
    HDC bound_dc;
    RECT bound_rect;
    YSEmojiFallbackItem fallback_queue[YS_EMOJI_FALLBACK_QUEUE_CAPACITY];
    UINT32 fallback_count;

    YSEmojiLayoutCacheSlot *layout_cache;
    UINT32 layout_cache_used;
    CRITICAL_SECTION cache_lock;

    /* Bitmap bake pipeline: a second render target configured to preserve
     * alpha (dc_target above uses D2D1_ALPHA_MODE_IGNORE, since it draws
     * directly onto the grid's already-opaque GDI-rendered background).
     * It needs its own D2D1_FACTORY_TYPE_MULTI_THREADED factory, separate
     * from d2d_factory (D2D1_FACTORY_TYPE_SINGLE_THREADED): baking runs
     * from both the UI thread and background warm-up worker threads, and
     * a single-threaded factory's resources may only ever be called from
     * the one thread that created them -- serializing access with our own
     * bake_lock is not enough, that only prevents concurrent calls, not
     * calls from a different thread than the creating one, which is
     * undefined behaviour for a single-threaded factory regardless of
     * concurrency. */
    ID2D1Factory *bake_factory;
    /* The UI thread's bake context; guarded by bake_lock. Warm-up workers
     * use their own private contexts and never touch this one. */
    YSEmojiBakeContext shared_bake;
    CRITICAL_SECTION bake_lock;
    BOOL bitmap_baking_available;
    int cell_width;
    int cell_height;
    YSEmojiBitmapCacheSlot *bitmap_cache;
    UINT32 bitmap_cache_used;
    ULONGLONG bitmap_cache_clock;
    /* Reusable DC for selecting a cached bitmap in order to AlphaBlend it;
     * blit_dc_stock is the 1x1 monochrome bitmap CreateCompatibleDC(NULL)
     * originally selected into it, restored before deleting blit_dc so a
     * cached bitmap is never still selected when it gets DeleteObject'd. */
    HDC blit_dc;
    HBITMAP blit_dc_stock;

    BOOL drawing;
    BOOL available;
};

static IDWriteTextLayout *ys_emoji_renderer_get_or_create_layout(YSEmojiRenderer *renderer, const WCHAR *text,
                                                                  FLOAT width, FLOAT height);

static UINT32 ys_emoji_text_hash(const WCHAR *text) {
    UINT32 hash = 2166136261u;
    while (*text) {
        hash ^= (UINT16)*text++;
        hash *= 16777619u;
    }
    return hash;
}

/* renderer->cache_lock must be held by the caller for every function in
 * this group -- they do no locking of their own so callers can hold the
 * lock across a find-then-maybe-insert sequence without deadlocking. */

static void ys_emoji_layout_cache_clear(YSEmojiRenderer *renderer) {
    UINT32 i;
    if (!renderer->layout_cache) return;
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

static void ys_emoji_bitmap_cache_clear(YSEmojiRenderer *renderer) {
    UINT32 i;
    if (!renderer->bitmap_cache) return;
    for (i = 0; i < YS_EMOJI_BITMAP_CACHE_CAPACITY; ++i) {
        if (renderer->bitmap_cache[i].bitmap) {
            DeleteObject(renderer->bitmap_cache[i].bitmap);
            renderer->bitmap_cache[i].bitmap = NULL;
            renderer->bitmap_cache[i].text[0] = 0;
            renderer->bitmap_cache[i].last_used = 0;
        }
    }
    renderer->bitmap_cache_used = 0;
    renderer->bitmap_cache_clock = 0;
}

static HBITMAP ys_emoji_bitmap_cache_find(YSEmojiRenderer *renderer, const WCHAR *text) {
    UINT32 slot;
    if (!renderer->bitmap_cache) return NULL;
    for (slot = 0; slot < YS_EMOJI_BITMAP_CACHE_CAPACITY; ++slot) {
        if (renderer->bitmap_cache[slot].bitmap &&
            wcscmp(renderer->bitmap_cache[slot].text, text) == 0) {
            renderer->bitmap_cache[slot].last_used = ++renderer->bitmap_cache_clock;
            return renderer->bitmap_cache[slot].bitmap;
        }
    }
    return NULL;
}

static BOOL ys_emoji_bitmap_cache_insert(YSEmojiRenderer *renderer, const WCHAR *text, HBITMAP bitmap) {
    UINT32 slot;
    UINT32 target = 0;
    ULONGLONG oldest = (ULONGLONG)-1;
    if (!renderer->bitmap_cache) return FALSE;
    for (slot = 0; slot < YS_EMOJI_BITMAP_CACHE_CAPACITY; ++slot) {
        if (!renderer->bitmap_cache[slot].bitmap) {
            target = slot;
            break;
        }
        if (renderer->bitmap_cache[slot].last_used < oldest) {
            oldest = renderer->bitmap_cache[slot].last_used;
            target = slot;
        }
    }
    if (renderer->bitmap_cache[target].bitmap) {
        DeleteObject(renderer->bitmap_cache[target].bitmap);
    } else {
        ++renderer->bitmap_cache_used;
    }
    StringCchCopyW(renderer->bitmap_cache[target].text, YS_EMOJI_LAYOUT_CACHE_TEXT_CAPACITY, text);
    renderer->bitmap_cache[target].bitmap = bitmap;
    renderer->bitmap_cache[target].last_used = ++renderer->bitmap_cache_clock;
    return TRUE;
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

/* Sets up everything the bitmap bake pipeline needs: a render target with
 * alpha preserved (unlike dc_target's IGNORE mode), its own brush, and the
 * reusable DC used later to AlphaBlend cached bitmaps out. Failure here
 * just leaves bitmap_baking_available FALSE -- ys_emoji_renderer_draw
 * falls back to drawing the cached IDWriteTextLayout directly, so the
 * renderer stays fully functional either way. */
static BOOL ys_emoji_bake_context_init(YSEmojiRenderer *renderer, YSEmojiBakeContext *context) {
    D2D1_RENDER_TARGET_PROPERTIES properties;
    D2D1_COLOR_F black;
    BITMAPINFO bmi;
    RECT bounds;
    HRESULT result;

    ZeroMemory(context, sizeof(*context));

    /* S_FALSE means COM was already initialized on this thread (still ours to
     * balance); RPC_E_CHANGED_MODE means someone initialized it with a
     * different apartment model -- WIC works either way, we just must not
     * call CoUninitialize in that case. */
    result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    context->com_initialized = SUCCEEDED(result);
    result = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                              IID_IWICImagingFactory, (void **)&context->wic);
    if (FAILED(result)) context->wic = NULL;   /* fall back to font rendering */
    context->dc = CreateCompatibleDC(NULL);
    if (!context->dc) return FALSE;
    context->dc_stock = (HBITMAP)GetCurrentObject(context->dc, OBJ_BITMAP);

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = renderer->cell_width;
    bmi.bmiHeader.biHeight = renderer->cell_height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    context->scratch = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &context->scratch_bits, NULL, 0);
    if (!context->scratch || !context->scratch_bits) return FALSE;
    SelectObject(context->dc, context->scratch);

    ZeroMemory(&properties, sizeof(properties));
    /* SOFTWARE, not DEFAULT: each bake is a tiny (one cell) render whose
     * result must end up in a CPU-side DIB. With a GPU-backed target every
     * EndDraw costs a GPU flush plus a readback, which measured far slower
     * than simply rasterizing these small color glyphs on the CPU -- and
     * this path bakes thousands of them. */
    properties.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    properties.dpiX = 96.0f;
    properties.dpiY = 96.0f;
    properties.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    properties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    result = renderer->bake_factory->CreateDCRenderTarget(&properties, &context->target);
    if (FAILED(result) || !context->target) return FALSE;

    black.r = 0.0f;
    black.g = 0.0f;
    black.b = 0.0f;
    black.a = 1.0f;
    result = context->target->CreateSolidColorBrush(&black, NULL, &context->brush);
    if (FAILED(result) || !context->brush) return FALSE;

    /* Bound once here, never per bake. */
    bounds.left = 0;
    bounds.top = 0;
    bounds.right = renderer->cell_width;
    bounds.bottom = renderer->cell_height;
    result = context->target->BindDC(context->dc, &bounds);
    if (FAILED(result)) return FALSE;
    return TRUE;
}

static void ys_emoji_bake_context_destroy(YSEmojiBakeContext *context) {
    if (context->wic) {
        context->wic->Release();
        context->wic = NULL;
    }
    if (context->brush) {
        context->brush->Release();
        context->brush = NULL;
    }
    if (context->target) {
        context->target->Release();
        context->target = NULL;
    }
    if (context->dc) {
        /* Restore the stock bitmap so the scratch DIB is not still
         * selected into a DC when it (and the DC) are deleted. */
        SelectObject(context->dc, context->dc_stock);
        DeleteDC(context->dc);
        context->dc = NULL;
    }
    if (context->scratch) {
        DeleteObject(context->scratch);
        context->scratch = NULL;
        context->scratch_bits = NULL;
    }
    if (context->com_initialized) {
        CoUninitialize();
        context->com_initialized = FALSE;
    }
}

static BOOL ys_emoji_renderer_init_bake_pipeline(YSEmojiRenderer *renderer) {
    HRESULT result;

    renderer->blit_dc = CreateCompatibleDC(NULL);
    if (!renderer->blit_dc) return FALSE;
    renderer->blit_dc_stock = (HBITMAP)GetCurrentObject(renderer->blit_dc, OBJ_BITMAP);

    renderer->bitmap_cache = (YSEmojiBitmapCacheSlot *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(YSEmojiBitmapCacheSlot) * YS_EMOJI_BITMAP_CACHE_CAPACITY);
    if (!renderer->bitmap_cache) return FALSE;

    result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               IID_ID2D1Factory, NULL,
                               (void **)&renderer->bake_factory);
    if (FAILED(result) || !renderer->bake_factory) return FALSE;

    return ys_emoji_bake_context_init(renderer, &renderer->shared_bake);
}

YSEmojiRenderer *ys_emoji_renderer_create(float font_size_pixels, int cell_width, int cell_height) {
    YSEmojiRenderer *renderer;
    HRESULT result;
    renderer = (YSEmojiRenderer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*renderer));
    if (!renderer) return NULL;
    /* Initialized before anything else so ys_emoji_renderer_destroy can
     * unconditionally delete/close them on any later failure path below. */
    InitializeCriticalSection(&renderer->cache_lock);
    InitializeCriticalSection(&renderer->bake_lock);
    renderer->cell_width = max(1, cell_width);
    renderer->cell_height = max(1, cell_height);

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

    /* Bitmap baking is a pure performance optimization on top of an
     * already-working layout-based renderer: if it fails to set up, leave
     * it off rather than failing the whole renderer. */
    renderer->bitmap_baking_available = ys_emoji_renderer_init_bake_pipeline(renderer);

    renderer->available = TRUE;
    return renderer;

failed:
    ys_emoji_renderer_destroy(renderer);
    return NULL;
}

void ys_emoji_renderer_destroy(YSEmojiRenderer *renderer) {
    if (!renderer) return;
    ys_emoji_renderer_release_target(renderer);
    ys_emoji_layout_cache_clear(renderer);
    if (renderer->layout_cache) HeapFree(GetProcessHeap(), 0, renderer->layout_cache);
    if (renderer->text_format) renderer->text_format->Release();
    if (renderer->dwrite_factory) renderer->dwrite_factory->Release();
    if (renderer->d2d_factory) renderer->d2d_factory->Release();

    /* Detach whatever bitmap is currently selected before freeing the cache
     * below, so no cached HBITMAP is still selected into a DC when
     * DeleteObject'd (that would silently fail to free it). */
    if (renderer->blit_dc) {
        SelectObject(renderer->blit_dc, renderer->blit_dc_stock);
        DeleteDC(renderer->blit_dc);
    }
    ys_emoji_bake_context_destroy(&renderer->shared_bake);
    ys_emoji_bitmap_cache_clear(renderer);
    if (renderer->bitmap_cache) HeapFree(GetProcessHeap(), 0, renderer->bitmap_cache);
    if (renderer->bake_factory) renderer->bake_factory->Release();

    DeleteCriticalSection(&renderer->bake_lock);
    DeleteCriticalSection(&renderer->cache_lock);
    HeapFree(GetProcessHeap(), 0, renderer);
}

BOOL ys_emoji_renderer_is_available(const YSEmojiRenderer *renderer) {
    return renderer && renderer->available && renderer->text_format;
}

/* Starts a batch.
 *
 * Note what this deliberately does NOT do: it does not BindDC/BeginDraw the
 * Direct2D target. An ID2D1DCRenderTarget snapshots the DC's current pixels
 * when a draw pass starts and blits its own surface back over that region
 * at EndDraw. Any GDI drawing done on the same DC *between* those two
 * points is therefore discarded -- which is exactly what happened when the
 * cached-bitmap AlphaBlend path ran inside an open D2D pass: every emoji
 * cell came out blank. So the batch now runs GDI blits first and defers all
 * Direct2D work to ys_emoji_renderer_end, where a single pass draws
 * whatever could not use a cached bitmap. */
BOOL ys_emoji_renderer_begin(YSEmojiRenderer *renderer, HDC dc, const RECT *bounds) {
    if (!ys_emoji_renderer_is_available(renderer) || !dc || !bounds ||
        bounds->right <= bounds->left || bounds->bottom <= bounds->top) return FALSE;
    renderer->bound_dc = dc;
    renderer->bound_rect = *bounds;
    renderer->fallback_count = 0;
    renderer->drawing = TRUE;
    return TRUE;
}

/* Draws every queued fallback symbol in one Direct2D pass and empties the
 * queue. Safe to call repeatedly: each pass is self-contained, so GDI blits
 * made before or after a pass are preserved (only blits made *during* one
 * would be lost). */
static BOOL ys_emoji_renderer_flush_fallback(YSEmojiRenderer *renderer) {
    D2D1_MATRIX_3X2_F identity;
    HRESULT result;
    UINT32 i;
    if (!renderer->fallback_count) return TRUE;
    if (!renderer->bound_dc || !ys_emoji_renderer_create_target(renderer)) {
        renderer->fallback_count = 0;
        return FALSE;
    }

    result = renderer->dc_target->BindDC(renderer->bound_dc, &renderer->bound_rect);
    if (FAILED(result)) {
        ys_emoji_renderer_release_target(renderer);
        renderer->fallback_count = 0;
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

    for (i = 0; i < renderer->fallback_count; ++i) {
        const YSEmojiFallbackItem *item = &renderer->fallback_queue[i];
        FLOAT width = (FLOAT)(item->rect.right - item->rect.left);
        FLOAT height = (FLOAT)(item->rect.bottom - item->rect.top);
        IDWriteTextLayout *layout;
        D2D1_COLOR_F color;
        D2D1_POINT_2F origin;
        D2D1_DRAW_TEXT_OPTIONS options;

        layout = ys_emoji_renderer_get_or_create_layout(renderer, item->text, width, height);
        if (!layout) continue;
        layout->SetMaxWidth(width);
        layout->SetMaxHeight(height);

        color.r = (float)GetRValue(item->color) / 255.0f;
        color.g = (float)GetGValue(item->color) / 255.0f;
        color.b = (float)GetBValue(item->color) / 255.0f;
        color.a = 1.0f;
        renderer->brush->SetColor(&color);

        origin.x = (FLOAT)item->rect.left;
        origin.y = (FLOAT)item->rect.top;
        options = (D2D1_DRAW_TEXT_OPTIONS)(D2D1_DRAW_TEXT_OPTIONS_CLIP |
                                           D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        renderer->dc_target->DrawTextLayout(origin, layout, renderer->brush, options);
    }
    renderer->fallback_count = 0;

    result = renderer->dc_target->EndDraw(NULL, NULL);
    if (FAILED(result)) {
        ys_emoji_renderer_release_target(renderer);
        return FALSE;
    }
    return TRUE;
}

/* Returns a cached (or freshly created-and-cached) layout for `text`, sized
 * to width x height. The returned pointer is owned by the cache -- never
 * Release() it.
 *
 * UI-thread only. Cached layouts are mutable shared state (callers call
 * SetMaxWidth/SetMaxHeight on them before drawing), so they must not be
 * handed to background threads -- the bake path deliberately builds its
 * own private layout instead, see ys_emoji_renderer_ensure_baked.
 *
 * Returns NULL only on genuine failure (CreateTextLayout failed, or the
 * cache is completely unavailable/full); callers should fall back to not
 * drawing this symbol this frame rather than leaking a layout that could
 * not be tracked. */
static IDWriteTextLayout *ys_emoji_renderer_get_or_create_layout(YSEmojiRenderer *renderer, const WCHAR *text,
                                                                  FLOAT width, FLOAT height) {
    IDWriteTextLayout *layout;
    HRESULT result;

    EnterCriticalSection(&renderer->cache_lock);
    layout = ys_emoji_layout_cache_find(renderer, text);
    LeaveCriticalSection(&renderer->cache_lock);
    if (layout) return layout;

    /* CreateTextLayout runs without the lock held so this (possibly slow,
     * first-time) work never blocks anyone else's cache access. */
    result = renderer->dwrite_factory->CreateTextLayout(text, (UINT32)wcslen(text),
                                                         renderer->text_format, width, height, &layout);
    if (FAILED(result) || !layout) return NULL;

    EnterCriticalSection(&renderer->cache_lock);
    if (renderer->layout_cache_used >= (YS_EMOJI_LAYOUT_CACHE_CAPACITY * 3u) / 4u) {
        ys_emoji_layout_cache_clear(renderer);
    }
    if (!ys_emoji_layout_cache_insert(renderer, text, layout)) {
        LeaveCriticalSection(&renderer->cache_lock);
        layout->Release();
        return NULL;
    }
    LeaveCriticalSection(&renderer->cache_lock);
    return layout;
}

/* Renders `layout` once into a fresh cell_width x cell_height, 32bpp
 * premultiplied-alpha bitmap using `context`, and returns it (caller owns
 * it -- insert into the bitmap cache or DeleteObject it). The caller must
 * guarantee exclusive use of `context` for the duration: pass a private
 * per-thread context, or the renderer's shared one while holding
 * bake_lock. Returns NULL on failure. */
static HBITMAP ys_emoji_renderer_bake(YSEmojiRenderer *renderer, YSEmojiBakeContext *context,
                                      IDWriteTextLayout *layout) {
    BITMAPINFO bmi;
    void *bits = NULL;
    HBITMAP dib;
    HRESULT result;
    D2D1_POINT_2F origin;
    size_t byte_count = (size_t)renderer->cell_width * (size_t)renderer->cell_height * 4u;

    origin.x = 0.0f;
    origin.y = 0.0f;
    context->target->BeginDraw();
    context->target->Clear(NULL);
    context->target->DrawTextLayout(origin, layout, context->brush,
                                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    result = context->target->EndDraw(NULL, NULL);
    if (FAILED(result)) return NULL;

    /* The scratch DIB is written through the DC, so flush GDI's batch
     * before reading its pixels back. */
    GdiFlush();

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = renderer->cell_width;
    bmi.bmiHeader.biHeight = renderer->cell_height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    dib = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        return NULL;
    }
    memcpy(bits, context->scratch_bits, byte_count);
    return dib;
}

static const YSEmojiImageRecord *ys_emoji_image_find(const WCHAR *text) {
    size_t i;
    for (i = 0; i < g_ys_emoji_image_count; ++i) {
        if (wcscmp(g_ys_emoji_images[i].text, text) == 0) return &g_ys_emoji_images[i];
    }
    return NULL;
}

/* Decodes an embedded PNG into a fresh cell-sized, 32bpp premultiplied-alpha
 * bitmap and returns it (caller owns it). The artwork is square while a grid
 * cell usually is not, so the image is scaled to the smaller cell dimension
 * and centred, rather than stretched out of shape. Returns NULL if anything
 * fails, so the caller can fall back to rendering the glyph from the font. */
static HBITMAP ys_emoji_renderer_decode_image(YSEmojiRenderer *renderer, YSEmojiBakeContext *context,
                                              const YSEmojiImageRecord *record) {
    IWICStream *stream = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *converter = NULL;
    BITMAPINFO bmi;
    void *bits = NULL;
    HBITMAP dib = NULL;
    unsigned char *pixels = NULL;
    UINT side, stride, buffer_size;
    int offset_x, offset_y, y;
    HRESULT result;

    if (!context->wic) return NULL;
    side = (UINT)min(YS_EMOJI_IMAGE_SIDE, min(renderer->cell_width, renderer->cell_height));
    if (side == 0) return NULL;

    result = context->wic->CreateStream(&stream);
    if (FAILED(result)) goto cleanup;
    result = stream->InitializeFromMemory((BYTE *)(g_ys_emoji_image_blob + record->offset),
                                          record->size);
    if (FAILED(result)) goto cleanup;
    result = context->wic->CreateDecoderFromStream(stream, NULL, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(result)) goto cleanup;
    result = decoder->GetFrame(0, &frame);
    if (FAILED(result)) goto cleanup;

    result = context->wic->CreateBitmapScaler(&scaler);
    if (FAILED(result)) goto cleanup;
    result = scaler->Initialize(frame, side, side, WICBitmapInterpolationModeFant);
    if (FAILED(result)) goto cleanup;

    result = context->wic->CreateFormatConverter(&converter);
    if (FAILED(result)) goto cleanup;
    /* PBGRA is exactly what AlphaBlend's AC_SRC_ALPHA expects. */
    result = converter->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(result)) goto cleanup;

    stride = side * 4u;
    buffer_size = stride * side;
    pixels = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, buffer_size);
    if (!pixels) goto cleanup;
    result = converter->CopyPixels(NULL, stride, buffer_size, pixels);
    if (FAILED(result)) goto cleanup;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = renderer->cell_width;
    /* Negative height: top-down, matching WIC's row order so the rows below
     * can be copied straight across without flipping. */
    bmi.bmiHeader.biHeight = -renderer->cell_height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    dib = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib || !bits) {
        if (dib) { DeleteObject(dib); dib = NULL; }
        goto cleanup;
    }
    ZeroMemory(bits, (size_t)renderer->cell_width * (size_t)renderer->cell_height * 4u);

    offset_x = (renderer->cell_width - (int)side) / 2;
    offset_y = (renderer->cell_height - (int)side) / 2;
    for (y = 0; y < (int)side; ++y) {
        unsigned char *dst = (unsigned char *)bits +
                             (size_t)(offset_y + y) * (size_t)renderer->cell_width * 4u +
                             (size_t)offset_x * 4u;
        memcpy(dst, pixels + (size_t)y * stride, stride);
    }

cleanup:
    if (pixels) HeapFree(GetProcessHeap(), 0, pixels);
    if (converter) converter->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    return dib;
}

/* Ensures `text` has a cached bitmap, baking one if needed. Safe to call
 * from any thread.
 *
 * `context` is the bake context to use: pass a private per-thread one
 * (warm-up workers) to bake without blocking anyone, or NULL to borrow the
 * renderer's shared context under bake_lock (UI thread). allow_evict must
 * only be TRUE for UI-thread callers (see the file-level comment on
 * eviction safety). Returns FALSE if bitmap baking is unavailable or
 * baking this symbol failed -- callers fall back to the layout-drawing
 * path in that case. */
static BOOL ys_emoji_renderer_ensure_baked(YSEmojiRenderer *renderer, const WCHAR *text) {
    IDWriteTextLayout *layout;
    HBITMAP bitmap = NULL;
    HBITMAP existing;
    const YSEmojiImageRecord *image;
    HRESULT result;
    if (!renderer->bitmap_baking_available || !text || !text[0]) return FALSE;

    EnterCriticalSection(&renderer->cache_lock);
    existing = ys_emoji_bitmap_cache_find(renderer, text);
    LeaveCriticalSection(&renderer->cache_lock);
    if (existing) return TRUE;

    /* Embedded artwork wins when we have it: it is a plain PNG decode rather
     * than a color-glyph rasterization, and for flags it is the only way to
     * get an actual flag on systems where the Region setting makes the font
     * fall back to bare letter pairs. */
    image = ys_emoji_image_find(text);
    if (image) {
        EnterCriticalSection(&renderer->bake_lock);
        bitmap = ys_emoji_renderer_decode_image(renderer, &renderer->shared_bake, image);
        LeaveCriticalSection(&renderer->bake_lock);
    }

    if (!bitmap) {
        /* Deliberately a private layout rather than a shared cached one:
         * cached layouts are mutated (SetMaxWidth/SetMaxHeight) by the UI
         * thread's draw path, and mutating one while a background thread is
         * drawing with it would be a data race. Baking happens once per
         * symbol, so the extra CreateTextLayout here costs nothing ongoing. */
        result = renderer->dwrite_factory->CreateTextLayout(text, (UINT32)wcslen(text),
                                                             renderer->text_format,
                                                             (FLOAT)renderer->cell_width,
                                                             (FLOAT)renderer->cell_height, &layout);
        if (FAILED(result) || !layout) return FALSE;

        EnterCriticalSection(&renderer->bake_lock);
        bitmap = ys_emoji_renderer_bake(renderer, &renderer->shared_bake, layout);
        LeaveCriticalSection(&renderer->bake_lock);
        layout->Release();
    }
    if (!bitmap) return FALSE;

    EnterCriticalSection(&renderer->cache_lock);
    existing = ys_emoji_bitmap_cache_find(renderer, text);
    if (existing) {
        LeaveCriticalSection(&renderer->cache_lock);
        DeleteObject(bitmap);
        return TRUE;
    }
    if (!ys_emoji_bitmap_cache_insert(renderer, text, bitmap)) DeleteObject(bitmap);
    LeaveCriticalSection(&renderer->cache_lock);
    return TRUE;
}

BOOL ys_emoji_renderer_draw(YSEmojiRenderer *renderer, const WCHAR *text,
                            const RECT *layout_rect, COLORREF fallback_color) {
    int dest_w, dest_h;
    BOOL has_embedded_image;
    if (!renderer || !renderer->drawing || !text || !text[0] || !layout_rect) return FALSE;

    dest_w = layout_rect->right - layout_rect->left;
    dest_h = layout_rect->bottom - layout_rect->top;
    if (dest_w <= 0 || dest_h <= 0) return FALSE;
    has_embedded_image = ys_emoji_image_find(text) != NULL;

    /* Fast path: a cached bitmap, blitted with plain GDI AlphaBlend, no
     * DirectWrite/Direct2D call at all. Embedded square artwork can be
     * centred and scaled to any requested rectangle, including the
     * differently-sized recent-use strip. Font-baked bitmaps remain limited
     * to rectangles close to the fixed bake size so text glyphs are not
     * distorted. */
    if (renderer->bitmap_baking_available && renderer->bound_dc &&
        (has_embedded_image ||
         (abs(dest_w - renderer->cell_width) <= 4 && abs(dest_h - renderer->cell_height) <= 4)) &&
        ys_emoji_renderer_ensure_baked(renderer, text)) {
        HBITMAP bitmap;
        EnterCriticalSection(&renderer->cache_lock);
        bitmap = ys_emoji_bitmap_cache_find(renderer, text);
        LeaveCriticalSection(&renderer->cache_lock);
        if (bitmap) {
            BLENDFUNCTION blend;
            HBITMAP previous;
            BOOL blended;
            int draw_x = layout_rect->left;
            int draw_y = layout_rect->top;
            int draw_w = dest_w;
            int draw_h = dest_h;
            int source_x = 0;
            int source_y = 0;
            int source_w = renderer->cell_width;
            int source_h = renderer->cell_height;
            if (has_embedded_image) {
                int draw_side = min(YS_EMOJI_IMAGE_SIDE, min(dest_w, dest_h));
                int source_side = min(YS_EMOJI_IMAGE_SIDE,
                                      min(renderer->cell_width, renderer->cell_height));
                draw_x += (dest_w - draw_side) / 2;
                draw_y += (dest_h - draw_side) / 2;
                draw_w = draw_h = draw_side;
                source_x = (renderer->cell_width - source_side) / 2;
                source_y = (renderer->cell_height - source_side) / 2;
                source_w = source_h = source_side;
            }
            ZeroMemory(&blend, sizeof(blend));
            blend.BlendOp = AC_SRC_OVER;
            blend.SourceConstantAlpha = 255;
            blend.AlphaFormat = AC_SRC_ALPHA;
            previous = (HBITMAP)SelectObject(renderer->blit_dc, bitmap);
            blended = AlphaBlend(renderer->bound_dc, draw_x, draw_y, draw_w, draw_h,
                                 renderer->blit_dc, source_x, source_y, source_w, source_h, blend);
            SelectObject(renderer->blit_dc, previous);
            /* On the (unexpected) chance AlphaBlend itself failed, fall
             * through to the layout path below instead of reporting a
             * successful draw that left the cell blank. */
            if (blended) return TRUE;
        }
    }

    /* Fallback path: queue this symbol for the deferred Direct2D pass in
     * ys_emoji_renderer_end. Used for differently-sized contexts, or if
     * bitmap baking is unavailable/this particular symbol failed to bake.
     * It cannot be drawn inline -- see ys_emoji_renderer_begin. */
    if (renderer->fallback_count >= YS_EMOJI_FALLBACK_QUEUE_CAPACITY) {
        /* Queue full: flush what we have so far and keep going. Flushing
         * mid-batch is safe because each Direct2D pass is self-contained. */
        ys_emoji_renderer_flush_fallback(renderer);
    }
    {
        YSEmojiFallbackItem *item = &renderer->fallback_queue[renderer->fallback_count++];
        StringCchCopyW(item->text, YS_EMOJI_LAYOUT_CACHE_TEXT_CAPACITY, text);
        item->rect = *layout_rect;
        item->color = fallback_color;
    }
    return TRUE;
}

BOOL ys_emoji_renderer_end(YSEmojiRenderer *renderer) {
    BOOL ok;
    if (!renderer || !renderer->drawing) return FALSE;
    /* All cached-bitmap blits for this batch are already on the DC; now run
     * the single deferred Direct2D pass for whatever could not use one.
     * On failure the target is dropped and recreated next paint, and GDI
     * remains the caller's fallback for the current frame. */
    ok = ys_emoji_renderer_flush_fallback(renderer);
    renderer->drawing = FALSE;
    renderer->bound_dc = NULL;
    return ok;
}
