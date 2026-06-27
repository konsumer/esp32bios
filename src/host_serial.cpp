/*
 * host_serial.cpp -- the simplest possible ESP32 host.
 *
 * Needs ZERO external libraries, so it runs on literally any ESP32 dev board.
 * It "draws" by streaming the ASCII framebuffer over the serial monitor, and
 * maps button A to the on-board BOOT button (GPIO0). Use it to bring up the
 * BIOS on new hardware before you have a display driver working.
 */
#ifdef BIOS_HOST_SERIAL

#include <Arduino.h>
#include "bios.h"

#define FB_W 128
#define FB_H 64
#define BTN_A_PIN 0          /* BOOT button on most ESP32 dev boards */

static uint8_t s_fb[FB_H][FB_W];
struct TextItem { int16_t x, y; char s[48]; };
static TextItem s_text[16];
static int      s_ntext;
static BiosTable g_bios;

static void     s_log(const char* m)   { Serial.print("[log] "); Serial.println(m); }
static uint32_t s_millis(void)         { return (uint32_t)millis(); }
static void     s_delay(uint32_t ms)   { delay(ms); }
static int16_t  s_width(void)          { return FB_W; }
static int16_t  s_height(void)         { return FB_H; }

static void s_clear(uint16_t color) {
    memset(s_fb, color ? 1 : 0, sizeof(s_fb));
    s_ntext = 0;
}
static void s_pixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return;
    s_fb[y][x] = color ? 1 : 0;
}
static void s_text(int16_t x, int16_t y, const char* s, uint16_t) {
    if (s_ntext >= 16) return;
    s_text[s_ntext].x = x; s_text[s_ntext].y = y;
    strncpy(s_text[s_ntext].s, s, sizeof(s_text[0].s) - 1);
    s_text[s_ntext].s[sizeof(s_text[0].s) - 1] = '\0';
    s_ntext++;
}
static void s_flush(void) {
    const int CW = FB_W / 2, CH = FB_H / 2;
    static char grid[FB_H / 2][FB_W / 2 + 1];
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
    for (int i = 0; i < s_ntext; i++) {
        int row = s_text[i].y / 2, col = s_text[i].x / 2;
        if (row < 0 || row >= CH) continue;
        for (int k = 0; s_text[i].s[k] && col + k < CW; k++)
            if (col + k >= 0) grid[row][col + k] = s_text[i].s[k];
    }
    Serial.print("\033[H");                          /* home (works in most terminals) */
    for (int r = 0; r < CH; r++) Serial.println(grid[r]);
}
static bool s_button(uint8_t id) {
    if (id == BIOS_BTN_A) return digitalRead(BTN_A_PIN) == LOW;
    return false;
}

void setup() {
    Serial.begin(115200);
    pinMode(BTN_A_PIN, INPUT_PULLUP);
    delay(300);
    Serial.print("\033[2J");

    g_bios.magic          = BIOS_MAGIC;
    g_bios.version        = BIOS_VERSION;
    g_bios.size           = sizeof(BiosTable);
    g_bios.log            = s_log;
    g_bios.millis         = s_millis;
    g_bios.delay_ms       = s_delay;
    g_bios.display_width  = s_width;
    g_bios.display_height = s_height;
    g_bios.display_clear  = s_clear;
    g_bios.display_pixel  = s_pixel;
    g_bios.display_text   = s_text;
    g_bios.display_flush  = s_flush;
    g_bios.button_pressed = s_button;

    app_setup(&g_bios);
}

void loop() {
    app_loop(&g_bios);
}

#endif /* BIOS_HOST_SERIAL */
