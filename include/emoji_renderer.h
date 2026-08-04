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

#ifdef __cplusplus
}
#endif
