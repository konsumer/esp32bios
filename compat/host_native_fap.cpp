/*
 * host_native_fap.cpp -- desktop host that runs a Flipper app over the compat
 * shim. It builds a BiosTable (ASCII renderer, 128x64 = Flipper's screen), hands
 * it to the shim, then calls the FAP entry point and lets the app run its own
 * loop -- exactly how Flipper firmware launches a FAP.
 *
 * To make the demo terminate on its own, the "Back" button reports pressed after
 * ~1s (a simulated user press), so the app exits cleanly and we see teardown.
 */
#include "bios.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>

/* the shim's hook + the app entry (resolved at link here; by name on device) */
extern "C" void compat_set_bios(const BiosTable* bios);
extern "C" int32_t hello_world_app(void* p);

#define FB_W 128
#define FB_H 64
static uint8_t s_fb[FB_H][FB_W];
struct TextItem { int16_t x, y; char s[48]; };
static TextItem s_text[24];
static int s_ntext;

static void     n_log(const char* m) { fprintf(stderr, "[log] %s\n", m); }
static uint32_t n_millis(void) { using namespace std::chrono;
    static auto t0 = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count(); }
static void     n_delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static int16_t  n_w(void) { return FB_W; }
static int16_t  n_h(void) { return FB_H; }
static void     n_clear(uint16_t c) { memset(s_fb, c ? 1 : 0, sizeof(s_fb)); s_ntext = 0; }
static void     n_pixel(int16_t x, int16_t y, uint16_t c) {
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return; s_fb[y][x] = c ? 1 : 0; }
static void     n_text(int16_t x, int16_t y, const char* s, uint16_t) {
    if (s_ntext >= 24) return;
    s_text[s_ntext].x = x; s_text[s_ntext].y = y;
    strncpy(s_text[s_ntext].s, s, sizeof(s_text[0].s) - 1);
    s_text[s_ntext].s[sizeof(s_text[0].s) - 1] = '\0'; s_ntext++; }
static void     n_flush(void) {
    const int CW = FB_W / 2, CH = FB_H / 2;
    char grid[CH][CW + 1];
    for (int r = 0; r < CH; r++) { for (int c = 0; c < CW; c++) {
        int lit = 0;
        for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++)
            if (s_fb[r*2+dy][c*2+dx]) lit = 1;
        grid[r][c] = lit ? '#' : ' '; } grid[r][CW] = '\0'; }
    for (int i = 0; i < s_ntext; i++) { int row = s_text[i].y/2, col = s_text[i].x/2;
        if (row < 0 || row >= CH) continue;
        for (int k = 0; s_text[i].s[k] && col+k < CW; k++) if (col+k >= 0) grid[row][col+k] = s_text[i].s[k]; }
    printf("\033[H"); for (int r = 0; r < CH; r++) printf("%s\n", grid[r]); fflush(stdout); }
static bool     n_button(uint8_t id) {
    /* simulate the user pressing Back (button B) ~1s in, so the app exits */
    return id == BIOS_BTN_B && n_millis() > 1000;
}

int main(void) {
    BiosTable bios;
    bios.magic = BIOS_MAGIC; bios.version = BIOS_VERSION; bios.size = sizeof(BiosTable);
    bios.log = n_log; bios.millis = n_millis; bios.delay_ms = n_delay;
    bios.display_width = n_w; bios.display_height = n_h;
    bios.display_clear = n_clear; bios.display_pixel = n_pixel;
    bios.display_text = n_text; bios.display_flush = n_flush;
    bios.button_pressed = n_button;
    bios.capability     = bios_no_caps;

    printf("\033[2J");
    compat_set_bios(&bios);

    fprintf(stderr, "[host] launching FAP hello_world_app()\n");
    int32_t rc = hello_world_app(NULL);
    fprintf(stderr, "[host] FAP returned %d (clean exit on Back)\n", (int)rc);
    return 0;
}
