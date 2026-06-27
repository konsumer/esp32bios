/*
 * host_flipper.cpp -- ESP32 firmware that loads and runs a Flipper Zero app.
 *
 * It is host_elf.cpp's sibling, but instead of an esp32bios-native app it loads
 * a Flipper app: the ELF's furi / gui / canvas imports are resolved against the
 * compat shim (compat_resolve), the shim is pointed at our BiosTable, and the
 * app's FAP entry point is called once (it runs its own event loop until exit).
 *
 * Build/run:
 *   loader/build_flipper_app.sh esp32          # or: s3   (compiles the FAP)
 *   pio run -e esp32-flipper   -t upload -t uploadfs -t monitor   # classic ESP32
 *   pio run -e cardputer-flipper -t upload -t uploadfs -t monitor # Cardputer screen
 *
 * Verified to compile for both targets; on-hardware execution is the part that
 * needs a board (see compat/README.md and INFO.md).
 */
#ifdef BIOS_HOST_FLIPPER

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include "bios.h"
#include "bios_caps.h"
#include "elfload.h"
#include "compat.h"

#define EM_XTENSA 94
#define BTN_A_PIN 0

/* The entry symbol the loader looks for. In real Flipper this comes from the
 * .fap manifest; here it's a build-time constant (override with -DFAP_ENTRY=...). */
#ifndef FAP_ENTRY
#define FAP_ENTRY "hello_world_app"
#endif

static BiosTable g_bios;

/* ============================ BIOS: display backend ======================== */
#ifdef FLIPPER_DISPLAY_M5
/* --- M5 color screen (Cardputer etc.) --- */
#include <M5Unified.h>
static int16_t  b_width(void)  { return (int16_t)M5.Display.width(); }
static int16_t  b_height(void) { return (int16_t)M5.Display.height(); }
static void     b_clear(uint16_t c)  { M5.Display.fillScreen(c); }
static void     b_pixel(int16_t x, int16_t y, uint16_t c) { M5.Display.drawPixel(x, y, c); }
static void     b_text(int16_t x, int16_t y, const char* s, uint16_t c) {
    M5.Display.setTextColor(c); M5.Display.setCursor(x, y); M5.Display.print(s); }
static void     b_flush(void) {}
static bool     b_button(uint8_t id) {
    M5.update();
    switch (id) { case BIOS_BTN_A: return M5.BtnA.isPressed();
                  case BIOS_BTN_B: return M5.BtnB.isPressed();
                  case BIOS_BTN_C: return M5.BtnC.isPressed(); default: return false; } }
static void     display_begin(void) { auto cfg = M5.config(); M5.begin(cfg); }

/* --- IR transmit capability: Cardputer IR LED on GPIO44, bit-banged carrier.
 * (RMT is the production path; bit-bang keeps the PoC simple and portable.) --- */
#define IR_TX_PIN 44
static bool ir_tx(const uint32_t* d, size_t n, uint32_t carrier_hz, float duty) {
    (void)duty;
    uint32_t half = carrier_hz ? (500000UL / carrier_hz) : 13;  /* half carrier period, us */
    for (size_t i = 0; i < n; i++) {
        uint32_t t_end = micros() + d[i];
        if (i % 2 == 0) {                       /* mark: drive modulated carrier */
            while ((int32_t)(t_end - micros()) > 0) {
                digitalWrite(IR_TX_PIN, HIGH); delayMicroseconds(half);
                digitalWrite(IR_TX_PIN, LOW);  delayMicroseconds(half);
            }
        } else {                                /* space: LED off */
            digitalWrite(IR_TX_PIN, LOW);
            while ((int32_t)(t_end - micros()) > 0) { }
        }
    }
    return true;
}
static const BiosCapInfrared g_ir_cap = { 1, ir_tx, 0, 0, 0 };
static const void* host_capability(uint32_t id) {
    return id == BIOS_CAP_INFRARED ? (const void*)&g_ir_cap : 0;
}
static void caps_begin(void) { pinMode(IR_TX_PIN, OUTPUT); digitalWrite(IR_TX_PIN, LOW); }

#else
/* --- serial ASCII screen (any ESP32) --- */
#define FB_W 128
#define FB_H 64
static uint8_t s_fb[FB_H][FB_W];
struct TextItem { int16_t x, y; char s[48]; };
static TextItem s_text[24];
static int s_ntext;
static int16_t  b_width(void)  { return FB_W; }
static int16_t  b_height(void) { return FB_H; }
static void     b_clear(uint16_t c) { memset(s_fb, c ? 1 : 0, sizeof(s_fb)); s_ntext = 0; }
static void     b_pixel(int16_t x, int16_t y, uint16_t c) {
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return; s_fb[y][x] = c ? 1 : 0; }
static void     b_text(int16_t x, int16_t y, const char* s, uint16_t) {
    if (s_ntext >= 24) return;
    s_text[s_ntext].x = x; s_text[s_ntext].y = y;
    strncpy(s_text[s_ntext].s, s, sizeof(s_text[0].s) - 1);
    s_text[s_ntext].s[sizeof(s_text[0].s) - 1] = '\0'; s_ntext++; }
static void     b_flush(void) {
    const int CW = FB_W / 2, CH = FB_H / 2;
    static char grid[FB_H/2][FB_W/2 + 1];
    for (int r = 0; r < CH; r++) { for (int c = 0; c < CW; c++) {
        int lit = 0;
        for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++)
            if (s_fb[r*2+dy][c*2+dx]) lit = 1;
        grid[r][c] = lit ? '#' : ' '; } grid[r][CW] = '\0'; }
    for (int i = 0; i < s_ntext; i++) { int row = s_text[i].y/2, col = s_text[i].x/2;
        if (row < 0 || row >= CH) continue;
        for (int k = 0; s_text[i].s[k] && col+k < CW; k++) if (col+k >= 0) grid[row][col+k] = s_text[i].s[k]; }
    Serial.print("\033[H"); for (int r = 0; r < CH; r++) Serial.println(grid[r]); }
static bool     b_button(uint8_t id) { return id == BIOS_BTN_A && digitalRead(BTN_A_PIN) == LOW; }
static void     display_begin(void) { pinMode(BTN_A_PIN, INPUT_PULLUP); Serial.print("\033[2J"); }
/* no known IR pin on a generic ESP32 board -> no peripherals advertised */
static const void* host_capability(uint32_t id) { return bios_no_caps(id); }
static void     caps_begin(void) {}
#endif

static void     b_log(const char* m) { Serial.print("[log] "); Serial.println(m); }
static uint32_t b_millis(void)       { return (uint32_t)millis(); }
static void     b_delay(uint32_t ms) { delay(ms); }

/* ============================ ELF loader callbacks ========================= */
static void* e_alloc_exec(size_t n) { return heap_caps_malloc((n + 3) & ~3u, MALLOC_CAP_EXEC); }
static void* e_alloc_data(size_t n) { return heap_caps_malloc(n ? n : 1, MALLOC_CAP_8BIT); }
static void  e_copy_exec(void* dst, const void* src, size_t n) {
    volatile uint32_t* d = (uint32_t*)dst; const uint8_t* s = (const uint8_t*)src;
    size_t words = n / 4, rem = n % 4;
    for (size_t i = 0; i < words; i++)
        d[i] = (uint32_t)s[i*4] | ((uint32_t)s[i*4+1]<<8) | ((uint32_t)s[i*4+2]<<16) | ((uint32_t)s[i*4+3]<<24);
    if (rem) { uint32_t w = 0; for (size_t b = 0; b < rem; b++) w |= (uint32_t)s[words*4+b] << (8*b); d[words] = w; }
}
/* Resolve the app's imports against the Flipper compat shim. */
static uintptr_t e_resolve(const char* name) {
    uintptr_t a = compat_resolve(name);
    if (a) return a;
    if (!strcmp(name, "memcpy"))  return (uintptr_t)&memcpy;
    if (!strcmp(name, "memset"))  return (uintptr_t)&memset;
    if (!strcmp(name, "strlen"))  return (uintptr_t)&strlen;
    return 0;
}
static void e_log(const char* m) { Serial.print("[elf] "); Serial.println(m); }

void setup() {
    Serial.begin(115200);
    delay(300);
    display_begin();
    caps_begin();

    g_bios.magic = BIOS_MAGIC; g_bios.version = BIOS_VERSION; g_bios.size = sizeof(BiosTable);
    g_bios.log = b_log; g_bios.millis = b_millis; g_bios.delay_ms = b_delay;
    g_bios.display_width = b_width; g_bios.display_height = b_height;
    g_bios.display_clear = b_clear; g_bios.display_pixel = b_pixel;
    g_bios.display_text = b_text; g_bios.display_flush = b_flush;
    g_bios.button_pressed = b_button;
    g_bios.capability = host_capability;

    if (!LittleFS.begin(false)) { Serial.println("[elf] LittleFS mount failed (uploadfs first)"); return; }

    File f = LittleFS.open("/app.elf", "r");
    if (!f) { Serial.println("[elf] /app.elf not found"); return; }
    size_t len = f.size();
    uint8_t* buf = (uint8_t*)malloc(len);
    f.read(buf, len); f.close();

    elf_env env;
    env.alloc_exec = e_alloc_exec; env.alloc_data = e_alloc_data; env.copy_exec = e_copy_exec;
    env.resolve = e_resolve; env.log = e_log;

    elf_symbol entries[1] = { { FAP_ENTRY, 0 } };
    int rc = elf_load(buf, len, &env, EM_XTENSA, entries, 1);
    free(buf);
    if (rc != ELF_OK) { Serial.printf("[elf] load failed: %d\n", rc); return; }

    Serial.printf("[elf] Flipper app loaded; entry %s @ %p\n", FAP_ENTRY, entries[0].addr);
    compat_set_bios(&g_bios);

    typedef int32_t (*fap_entry_t)(void*);
    fap_entry_t entry = (fap_entry_t)entries[0].addr;
    int32_t ret = entry(NULL);                  /* runs the app's own loop until it exits */
    Serial.printf("[elf] Flipper app returned %d\n", (int)ret);
}

void loop() { delay(1000); }

#endif /* BIOS_HOST_FLIPPER */
