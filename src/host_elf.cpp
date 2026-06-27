/*
 * host_elf.cpp -- ESP32 host that loads a separately compiled app at runtime
 * from the flash filesystem, with NO reflashing of the firmware.
 *
 * Flow:
 *   1. Build the BIOS jump table (renders over Serial here, like host_serial).
 *   2. Read /app.elf (a relocatable Xtensa object) from LittleFS.
 *   3. elf_load(): place sections into IRAM (.text) + DRAM (.rodata/.bss),
 *      apply relocations, find app_setup/app_loop.
 *   4. Call them, passing the BIOS table -- exactly like every other host.
 *
 * To produce and upload the app:
 *   loader/build_esp32_app.sh           # compiles src/app.cpp -> data/app.elf
 *   pio run -e esp32-elf -t uploadfs    # flashes the LittleFS image
 *   pio run -e esp32-elf -t upload -t monitor
 *
 * Verified on the desktop against a real Xtensa object (loader/test_elfload.cpp).
 * The execution step itself can only be confirmed on a board.
 */
#ifdef BIOS_HOST_ELF

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include "bios.h"
#include "elfload.h"

#define EM_XTENSA 94
#define FB_W 128
#define FB_H 64
#define BTN_A_PIN 0

/* ---- the BIOS the loaded app will call (compact Serial renderer) ----------- */
static uint8_t s_fb[FB_H][FB_W];
struct TextItem { int16_t x, y; char s[48]; };
static TextItem s_text[16];
static int      s_ntext;
static BiosTable g_bios;

static void     b_log(const char* m)   { Serial.print("[log] "); Serial.println(m); }
static uint32_t b_millis(void)         { return (uint32_t)millis(); }
static void     b_delay(uint32_t ms)   { delay(ms); }
static int16_t  b_width(void)          { return FB_W; }
static int16_t  b_height(void)         { return FB_H; }
static void     b_clear(uint16_t c)    { memset(s_fb, c ? 1 : 0, sizeof(s_fb)); s_ntext = 0; }
static void     b_pixel(int16_t x, int16_t y, uint16_t c) {
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return; s_fb[y][x] = c ? 1 : 0; }
static void     b_text(int16_t x, int16_t y, const char* s, uint16_t) {
    if (s_ntext >= 16) return;
    s_text[s_ntext].x = x; s_text[s_ntext].y = y;
    strncpy(s_text[s_ntext].s, s, sizeof(s_text[0].s) - 1);
    s_text[s_ntext].s[sizeof(s_text[0].s) - 1] = '\0'; s_ntext++; }
static void     b_flush(void) {
    const int CW = FB_W / 2, CH = FB_H / 2;
    static char grid[FB_H / 2][FB_W / 2 + 1];
    for (int r = 0; r < CH; r++) { for (int c = 0; c < CW; c++) {
        int lit = 0;
        for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++)
            if (s_fb[r*2+dy][c*2+dx]) lit = 1;
        grid[r][c] = lit ? '#' : ' '; } grid[r][CW] = '\0'; }
    for (int i = 0; i < s_ntext; i++) { int row = s_text[i].y/2, col = s_text[i].x/2;
        if (row < 0 || row >= CH) continue;
        for (int k = 0; s_text[i].s[k] && col+k < CW; k++) if (col+k >= 0) grid[row][col+k] = s_text[i].s[k]; }
    Serial.print("\033[H"); for (int r = 0; r < CH; r++) Serial.println(grid[r]); }
static bool     b_button(uint8_t id)   { return id == BIOS_BTN_A && digitalRead(BTN_A_PIN) == LOW; }

/* ---- elf_env: where the ESP32-specific memory handling lives --------------- */
static void* e_alloc_exec(size_t n) {
    /* IRAM: executable, 32-bit-access only -> round up, copy with words. */
    return heap_caps_malloc((n + 3) & ~3u, MALLOC_CAP_EXEC);
}
static void* e_alloc_data(size_t n) {
    return heap_caps_malloc(n ? n : 1, MALLOC_CAP_8BIT);   /* byte-addressable DRAM */
}
static void e_copy_exec(void* dst, const void* src, size_t n) {
    volatile uint32_t* d = (uint32_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    size_t words = n / 4, rem = n % 4;
    for (size_t i = 0; i < words; i++)
        d[i] = (uint32_t)s[i*4] | ((uint32_t)s[i*4+1] << 8) |
               ((uint32_t)s[i*4+2] << 16) | ((uint32_t)s[i*4+3] << 24);
    if (rem) {
        uint32_t w = 0;
        for (size_t b = 0; b < rem; b++) w |= (uint32_t)s[words*4 + b] << (8*b);
        d[words] = w;
    }
}
static uintptr_t e_resolve(const char* name) {
    /* Host symbol table for any stragglers. A self-contained app needs none. */
    if (!strcmp(name, "memcpy"))  return (uintptr_t)&memcpy;
    if (!strcmp(name, "memset"))  return (uintptr_t)&memset;
    if (!strcmp(name, "strncpy")) return (uintptr_t)&strncpy;
    return 0;
}
static void e_log(const char* m) { Serial.print("[elf] "); Serial.println(m); }

static elf_app g_app;
static bool    g_loaded = false;

static bool load_app(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) { Serial.printf("[elf] cannot open %s\n", path); return false; }
    size_t len = f.size();
    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) { Serial.println("[elf] out of memory for image"); f.close(); return false; }
    f.read(buf, len); f.close();
    Serial.printf("[elf] read %u bytes from %s\n", (unsigned)len, path);

    elf_env env;
    env.alloc_exec = e_alloc_exec;
    env.alloc_data = e_alloc_data;
    env.copy_exec  = e_copy_exec;
    env.resolve    = e_resolve;
    env.log        = e_log;

    int rc = elf_load(buf, len, &env, EM_XTENSA, &g_app);
    free(buf);                                  /* sections are copied; image no longer needed */
    if (rc != ELF_OK) { Serial.printf("[elf] load failed: %d\n", rc); return false; }
    Serial.printf("[elf] loaded; app_setup=%p app_loop=%p\n",
                  (void*)g_app.app_setup, (void*)g_app.app_loop);
    return true;
}

void setup() {
    Serial.begin(115200);
    pinMode(BTN_A_PIN, INPUT_PULLUP);
    delay(300);
    Serial.print("\033[2J");

    g_bios.magic = BIOS_MAGIC; g_bios.version = BIOS_VERSION; g_bios.size = sizeof(BiosTable);
    g_bios.log = b_log; g_bios.millis = b_millis; g_bios.delay_ms = b_delay;
    g_bios.display_width = b_width; g_bios.display_height = b_height;
    g_bios.display_clear = b_clear; g_bios.display_pixel = b_pixel;
    g_bios.display_text = b_text; g_bios.display_flush = b_flush;
    g_bios.button_pressed = b_button;

    if (!LittleFS.begin(false)) {
        Serial.println("[elf] LittleFS mount failed (run -t uploadfs first)");
        return;
    }
    if (!load_app("/app.elf")) return;

    g_loaded = true;
    g_app.app_setup(&g_bios);            /* dependency injection, same as every host */
}

void loop() {
    if (g_loaded) g_app.app_loop(&g_bios);
    else          delay(1000);
}

#endif /* BIOS_HOST_ELF */
