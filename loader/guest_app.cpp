/*
 * guest_app.cpp -- a program compiled as a STANDALONE binary (a .so), with no
 * link against the host at all. It is the purest form of what you originally
 * asked for: the app "looks up where the functions are" at runtime and calls
 * them, and the same binary file drops onto any host.
 *
 * It depends on exactly two symbols from the outside world:
 *   - bios_vector_discover()  (resolved against the host when it loads us)
 * ...and the host calls our two exported entry points by name (guest_setup /
 * guest_loop). Nothing else is shared at link time.
 *
 * Built independently:
 *   g++ -std=c++17 -Iinclude -shared -fPIC loader/guest_app.cpp -o app.so
 */
#include "bios_vector.h"

extern "C" {

static const BiosTable* s_bios = 0;
static int16_t px, py, vx, vy;

/* Exported: the host dlsym()s these by name. No struct, no shared header beyond
 * the contract, no link dependency. */
void guest_setup(void)
{
    s_bios = bios_vector_discover();          /* find the BIOS via the slot */
    if (!s_bios) return;
    s_bios->log("guest.so: discovered the BIOS, I am hardware-agnostic");
    px = 8; py = 8; vx = 1; vy = 1;
}

void guest_loop(void)
{
    const BiosTable* b = s_bios;
    if (!b) return;
    const int16_t w = b->display_width();
    const int16_t h = b->display_height();

    b->display_clear(BIOS_BLACK);
    for (int16_t x = 0; x < w; x++) { b->display_pixel(x, 0, BIOS_WHITE); b->display_pixel(x, h - 1, BIOS_WHITE); }
    for (int16_t y = 0; y < h; y++) { b->display_pixel(0, y, BIOS_WHITE); b->display_pixel(w - 1, y, BIOS_WHITE); }

    px += vx; py += vy;
    if (px <= 1 || px >= w - 2) vx = -vx;
    if (py <= 1 || py >= h - 2) vy = -vy;
    b->display_pixel(px, py, BIOS_WHITE);

    b->display_text(6, 6, "LOADED .so", BIOS_WHITE);
    b->display_flush();
    b->delay_ms(33);
}

} /* extern "C" */
