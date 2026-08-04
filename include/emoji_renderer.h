#pragma once

#include "yesymbol.h"

typedef struct YSEmojiRenderer YSEmojiRenderer;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DirectWrite + Direct2D color-font renderer.
 *
 * The renderer draws into an existing HDC, including the memory DC used by
 * YeSymbol's double-buffered symbol grids. If DirectWrite/Direct2D cannot be
 * initialized, callers keep using the existing GDI fallback path.
 */
YSEmojiRenderer *ys_emoji_renderer_create(float font_size_pixels);
void ys_emoji_renderer_destroy(YSEmojiRenderer *renderer);
BOOL ys_emoji_renderer_is_available(const YSEmojiRenderer *renderer);
BOOL ys_emoji_renderer_begin(YSEmojiRenderer *renderer, HDC dc, const RECT *bounds);
BOOL ys_emoji_renderer_draw(YSEmojiRenderer *renderer, const WCHAR *text,
                            const RECT *layout_rect, COLORREF fallback_color);
BOOL ys_emoji_renderer_end(YSEmojiRenderer *renderer);

/*
 * Pre-creates and caches the DirectWrite text layout for every string in
 * `texts` on a background thread, so the first real draw of each symbol is
 * already a cache hit instead of paying full text shaping on demand. Only
 * call this with pointers that stay valid for the renderer's entire
 * lifetime (e.g. the compiled-in symbol string pool) -- the background
 * thread reads them at its own pace. Takes ownership of the `texts` array
 * itself (freed on the background thread once consumed, or immediately if
 * the thread fails to start) but not of the strings it points to.
 * ys_emoji_renderer_destroy() cancels and joins the thread before releasing
 * any other resources, so it is always safe to destroy the renderer while
 * warming is still in progress.
 */
BOOL ys_emoji_renderer_warm_cache_async(YSEmojiRenderer *renderer,
                                        const WCHAR **texts, size_t text_count,
                                        int cell_width, int cell_height);

/*
 * Same effect as ys_emoji_renderer_warm_cache_async, but runs synchronously
 * on the calling thread and does not take ownership of `texts` (the caller
 * keeps it and may free/reuse it as soon as this call returns). Intended
 * for a small, bounded slice warmed at WM_CREATE time, before the window is
 * shown, so that a click into that slice's category is a cache hit even if
 * it happens before the background pass (see
 * ys_emoji_renderer_warm_cache_async) has caught up.
 */
void ys_emoji_renderer_warm_cache_sync(YSEmojiRenderer *renderer,
                                       const WCHAR *const *texts, size_t text_count,
                                       int cell_width, int cell_height);

#ifdef __cplusplus
}
#endif
