/*
 * app.cpp -- the "program".
 *
 * Notice what is NOT here: no <Arduino.h>, no display driver, no pin numbers,
 * no board #ifdefs. It includes exactly one thing: the BIOS contract. The same
 * object code runs on every host that satisfies that contract.
 *
 * It draws a bordered box with a pixel bouncing inside it and some text, and
 * reacts to button A.
 */
#include "bios.h"

static int16_t  px, py, vx, vy;
static uint32_t frames;

void app_setup(const BiosTable* bios)
{
    /* Defensive: make sure we were handed a table we understand. */
    if (!bios || bios->magic != BIOS_MAGIC || bios->version != BIOS_VERSION) {
        if (bios && bios->log) bios->log("app: incompatible BIOS!");
        return;
    }
    bios->log("app: hello from the hardware-agnostic program");

    px = 8; py = 8; vx = 1; vy = 1; frames = 0;
}

void app_loop(const BiosTable* bios)
{
    const int16_t w = bios->display_width();
    const int16_t h = bios->display_height();

    bios->display_clear(BIOS_BLACK);

    /* Border. */
    for (int16_t x = 0; x < w; x++) {
        bios->display_pixel(x, 0,     BIOS_WHITE);
        bios->display_pixel(x, h - 1, BIOS_WHITE);
    }
    for (int16_t y = 0; y < h; y++) {
        bios->display_pixel(0,     y, BIOS_WHITE);
        bios->display_pixel(w - 1, y, BIOS_WHITE);
    }

    /* Bounce the pixel. */
    px += vx; py += vy;
    if (px <= 1)     { px = 1;     vx = 1;  }
    if (px >= w - 2) { px = w - 2; vx = -1; }
    if (py <= 1)     { py = 1;     vy = 1;  }
    if (py >= h - 2) { py = h - 2; vy = -1; }
    bios->display_pixel(px, py, BIOS_WHITE);

    bios->display_text(6, 6,  "ESP32 BIOS", BIOS_WHITE);

    if (bios->button_pressed(BIOS_BTN_A))
        bios->display_text(6, 18, "BTN A!", BIOS_GREEN);

    bios->display_flush();
    bios->delay_ms(33);   /* ~30 fps */
    frames++;
}
