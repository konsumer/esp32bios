/*
 * host_native.cpp -- a host that runs on your desktop (PlatformIO `native`).
 *
 * It implements the BIOS contract against an in-memory framebuffer and renders
 * it to the terminal as ASCII. Great for developing apps with zero hardware:
 * the *exact same* app.cpp also runs here.
 */
#ifdef BIOS_HOST_NATIVE

#include "bios.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>

#define FB_W 128
#define FB_H 64

static uint8_t  s_fb[FB_H][FB_W];

/* Text is its own primitive in the contract, so we don't rasterize a font here:
 * we remember the strings and stamp them onto the ASCII view at flush time. */
struct TextItem { int16_t x, y; char s[48]; };
static TextItem s_text[16];
static int      s_ntext;

static void     n_log(const char* m)            { fprintf(stderr, "[log] %s\n", m); }
static uint32_t n_millis(void) {
    using namespace std::chrono;
    static auto t0 = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
static void     n_delay(uint32_t ms)            { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static int16_t  n_width(void)                   { return FB_W; }
static int16_t  n_height(void)                  { return FB_H; }

static void n_clear(uint16_t color) {
    memset(s_fb, color ? 1 : 0, sizeof(s_fb));
    s_ntext = 0;
}
static void n_pixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return;
    s_fb[y][x] = color ? 1 : 0;
}
static void n_text(int16_t x, int16_t y, const char* s, uint16_t /*color*/) {
    if (s_ntext >= 16) return;
    s_text[s_ntext].x = x;
    s_text[s_ntext].y = y;
    strncpy(s_text[s_ntext].s, s, sizeof(s_text[0].s) - 1);
    s_text[s_ntext].s[sizeof(s_text[0].s) - 1] = '\0';
    s_ntext++;
}
static void n_flush(void) {
    /* Downsample the 128x64 framebuffer 2x2 -> a 64x32 character grid. */
    const int CW = FB_W / 2, CH = FB_H / 2;
    char grid[CH][CW + 1];
    for (int r = 0; r < CH; r++) {
        for (int c = 0; c < CW; c++) {
            int lit = 0;
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++)
                    if (s_fb[r * 2 + dy][c * 2 + dx]) lit = 1;
            grid[r][c] = lit ? '#' : ' ';
        }
        grid[r][CW] = '\0';
    }
    /* Overlay text (positions are in display pixels -> /2 for the char grid). */
    for (int i = 0; i < s_ntext; i++) {
        int row = s_text[i].y / 2, col = s_text[i].x / 2;
        if (row < 0 || row >= CH) continue;
        for (int k = 0; s_text[i].s[k] && col + k < CW; k++)
            if (col + k >= 0) grid[row][col + k] = s_text[i].s[k];
    }
    printf("\033[H");                 /* cursor home -> redraw in place */
    for (int r = 0; r < CH; r++) printf("%s\n", grid[r]);
    fflush(stdout);
}
static bool n_button(uint8_t /*id*/) { return false; }   /* no input on desktop */

int main(void) {
    BiosTable bios;
    bios.magic          = BIOS_MAGIC;
    bios.version        = BIOS_VERSION;
    bios.size           = sizeof(BiosTable);
    bios.log            = n_log;
    bios.millis         = n_millis;
    bios.delay_ms       = n_delay;
    bios.display_width  = n_width;
    bios.display_height = n_height;
    bios.display_clear  = n_clear;
    bios.display_pixel  = n_pixel;
    bios.display_text   = n_text;
    bios.display_flush  = n_flush;
    bios.button_pressed = n_button;

    printf("\033[2J");                /* clear screen once */
    app_setup(&bios);
    for (;;) app_loop(&bios);
    return 0;
}

#endif /* BIOS_HOST_NATIVE */
