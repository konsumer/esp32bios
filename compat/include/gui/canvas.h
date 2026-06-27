/*
 * gui/canvas.h -- Flipper canvas API (shim). Signatures match Flipper's real
 * canvas.h so draw callbacks compile unchanged; implemented over BiosTable.
 */
#ifndef COMPAT_CANVAS_H
#define COMPAT_CANVAS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Canvas Canvas;

typedef enum { ColorWhite = 0x00, ColorBlack = 0x01, ColorXOR = 0x02 } Color;

typedef enum {
    FontPrimary,
    FontSecondary,
    FontKeyboard,
    FontBigNumbers,
    FontTotalNumber,
} Font;

typedef enum { AlignLeft, AlignRight, AlignTop, AlignBottom, AlignCenter } Align;

typedef enum {
    CanvasDirectionLeftToRight,
    CanvasDirectionTopToBottom,
    CanvasDirectionRightToLeft,
    CanvasDirectionBottomToTop,
} CanvasDirection;

void   canvas_clear(Canvas* canvas);
void   canvas_commit(Canvas* canvas);
size_t canvas_width(const Canvas* canvas);
size_t canvas_height(const Canvas* canvas);
size_t canvas_current_font_height(const Canvas* canvas);
void   canvas_set_color(Canvas* canvas, Color color);
void   canvas_set_font(Canvas* canvas, Font font);
void   canvas_draw_dot(Canvas* canvas, int32_t x, int32_t y);
void   canvas_draw_line(Canvas* canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void   canvas_draw_box(Canvas* canvas, int32_t x, int32_t y, size_t w, size_t h);
void   canvas_draw_frame(Canvas* canvas, int32_t x, int32_t y, size_t w, size_t h);
void   canvas_draw_str(Canvas* canvas, int32_t x, int32_t y, const char* str);
void   canvas_draw_str_aligned(
    Canvas* canvas, int32_t x, int32_t y, Align h, Align v, const char* str);
uint16_t canvas_string_width(Canvas* canvas, const char* str);

#ifdef __cplusplus
}
#endif

#endif
