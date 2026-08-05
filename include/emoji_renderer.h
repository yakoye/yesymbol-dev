#pragma once

#include "yesymbol.h"

typedef struct YSEmojiRenderer YSEmojiRenderer;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DirectWrite + Direct2D color-font renderer, backed by a per-symbol
 * bounded, on-demand bitmap cache.
 *
 * Drawing a color-font glyph through DirectWrite/Direct2D on every repaint
 * (BindDC/BeginDraw/DrawTextLayout/EndDraw) pays real per-call cost even
 * when the IDWriteTextLayout itself is cached and reused, because the
 * layout only records which glyphs to use -- the actual glyph
 * rasterization (extracting the color font's COLR/CPAL or embedded-bitmap
 * data into pixels) still happens fresh on every draw. This renderer bakes
 * each distinct symbol to a small 32bpp premultiplied-alpha bitmap exactly
 * once on first draw, and every subsequent draw of that symbol is a
 * plain GDI AlphaBlend from the cached bitmap -- no DirectWrite/Direct2D
 * call at all.
 *
 * cell_width/cell_height fix the size bitmaps are baked at (pass the main
 * symbol grid's cell size). ys_emoji_renderer_draw still accepts any
 * requested rect: embedded artwork uses the fast bitmap path as a centred
 * 36x36 image (or smaller when the target cannot fit it), including the
 * recent-use strip. Font-baked fallback bitmaps use the fast path only near
 * cell_width/cell_height; substantially different sizes still use the
 * cached IDWriteTextLayout.
 *
 * The renderer draws into an existing HDC, including the memory DC used by
 * YeSymbol's double-buffered symbol grids. If DirectWrite/Direct2D cannot be
 * initialized, callers keep using the existing GDI fallback path.
 */
YSEmojiRenderer *ys_emoji_renderer_create(float font_size_pixels, int cell_width, int cell_height);
void ys_emoji_renderer_destroy(YSEmojiRenderer *renderer);
BOOL ys_emoji_renderer_is_available(const YSEmojiRenderer *renderer);
BOOL ys_emoji_renderer_begin(YSEmojiRenderer *renderer, HDC dc, const RECT *bounds);
BOOL ys_emoji_renderer_draw(YSEmojiRenderer *renderer, const WCHAR *text,
                            const RECT *layout_rect, COLORREF fallback_color);
BOOL ys_emoji_renderer_end(YSEmojiRenderer *renderer);

#ifdef __cplusplus
}
#endif
