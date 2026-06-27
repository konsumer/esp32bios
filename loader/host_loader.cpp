/*
 * host_loader.cpp -- a host that loads a separately compiled app at runtime.
 *
 * This is the desktop stand-in for an on-device ELF loader. The host knows
 * NOTHING about the app at compile time -- it takes the app's path on the
 * command line, dlopen()s it, looks up guest_setup/guest_loop by name, and runs
 * them. The app finds the BIOS through the published-pointer slot. Neither side
 * was linked against the other.
 *
 *   ./host_loader app.so [frames]
 *
 * Built with -rdynamic so the app can resolve bios_vector_discover() (and any
 * future host-exported BIOS helpers) against us -- the same job the ELF loader's
 * symbol table does on the ESP32.
 */
#include "bios_vector.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <dlfcn.h>

/* ---- a real-ish host: ASCII framebuffer renderer (same idea as host_native) */
#define FB_W 128
#define FB_H 64
static uint8_t s_fb[FB_H][FB_W];
struct TextItem { int16_t x, y; char s[48]; };
static TextItem s_text[16];
static int s_ntext;

static void     h_log(const char* m)  { fprintf(stderr, "[log] %s\n", m); }
static uint32_t h_millis(void) { using namespace std::chrono;
    static auto t0 = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count(); }
static void     h_delay(uint32_t ms)  { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static int16_t  h_w(void) { return FB_W; }
static int16_t  h_h(void) { return FB_H; }
static void     h_clear(uint16_t c)   { memset(s_fb, c ? 1 : 0, sizeof(s_fb)); s_ntext = 0; }
static void     h_pixel(int16_t x, int16_t y, uint16_t c) {
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return; s_fb[y][x] = c ? 1 : 0; }
static void     h_text(int16_t x, int16_t y, const char* s, uint16_t) {
    if (s_ntext >= 16) return;
    s_text[s_ntext].x = x; s_text[s_ntext].y = y;
    strncpy(s_text[s_ntext].s, s, sizeof(s_text[0].s) - 1);
    s_text[s_ntext].s[sizeof(s_text[0].s) - 1] = '\0'; s_ntext++; }
static void     h_flush(void) {
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
static bool     h_button(uint8_t) { return false; }

static BiosTable g_bios;

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <app.so> [frames]\n", argv[0]); return 1; }
    const char* path   = argv[1];
    long frames        = (argc > 2) ? strtol(argv[2], 0, 10) : -1;   /* -1 = forever */

    /* 1. Build the jump table and publish it to the discovery slot. */
    g_bios.magic = BIOS_MAGIC; g_bios.version = BIOS_VERSION; g_bios.size = sizeof(BiosTable);
    g_bios.log = h_log; g_bios.millis = h_millis; g_bios.delay_ms = h_delay;
    g_bios.display_width = h_w; g_bios.display_height = h_h;
    g_bios.display_clear = h_clear; g_bios.display_pixel = h_pixel;
    g_bios.display_text = h_text; g_bios.display_flush = h_flush;
    g_bios.button_pressed = h_button;
    bios_vector_publish(&g_bios);

    /* 2. Load the app binary we were never linked against. */
    void* mod = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!mod) { fprintf(stderr, "load failed: %s\n", dlerror()); return 1; }

    /* 3. Resolve its entry points by name -- like an ELF loader's exports. */
    void (*guest_setup)(void) = (void(*)(void))dlsym(mod, "guest_setup");
    void (*guest_loop)(void)  = (void(*)(void))dlsym(mod, "guest_loop");
    if (!guest_setup || !guest_loop) {
        fprintf(stderr, "missing entry points: %s\n", dlerror()); dlclose(mod); return 1; }

    fprintf(stderr, "[loader] %s loaded; running\n", path);
    printf("\033[2J");

    /* 4. Run it. The app discovers the BIOS itself and calls through it. */
    guest_setup();
    for (long f = 0; frames < 0 || f < frames; f++) guest_loop();

    dlclose(mod);
    return 0;
}
