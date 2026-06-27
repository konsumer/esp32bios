/*
 * host_m5.cpp -- host for M5Stack devices (Core/Core2/Fire/CoreS3/StickC...).
 *
 * M5Unified abstracts the panel and the physical buttons across the whole M5
 * line, so this one host file covers many boards. The app sees the same
 * contract; here it gets a real color TFT and three hardware buttons.
 */
#ifdef BIOS_HOST_M5

#include <M5Unified.h>
#include "bios.h"

static BiosTable g_bios;

static void     m_log(const char* msg)  { Serial.print("[log] "); Serial.println(msg); }
static uint32_t m_millis(void)          { return (uint32_t)millis(); }
static void     m_delay(uint32_t ms)    { delay(ms); }
static int16_t  m_width(void)           { return (int16_t)M5.Display.width(); }
static int16_t  m_height(void)          { return (int16_t)M5.Display.height(); }
static void     m_clear(uint16_t c)     { M5.Display.fillScreen(c); }   /* RGB565 native */
static void     m_pixel(int16_t x, int16_t y, uint16_t c) { M5.Display.drawPixel(x, y, c); }
static void     m_text(int16_t x, int16_t y, const char* s, uint16_t c) {
    M5.Display.setTextColor(c);
    M5.Display.setCursor(x, y);
    M5.Display.print(s);
}
static void     m_flush(void)           { /* M5.Display draws immediately */ }
static bool     m_button(uint8_t id) {
    M5.update();                         /* refresh button states */
    switch (id) {
        case BIOS_BTN_A: return M5.BtnA.isPressed();
        case BIOS_BTN_B: return M5.BtnB.isPressed();
        case BIOS_BTN_C: return M5.BtnC.isPressed();
        default:         return false;
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    g_bios.magic          = BIOS_MAGIC;
    g_bios.version        = BIOS_VERSION;
    g_bios.size           = sizeof(BiosTable);
    g_bios.log            = m_log;
    g_bios.millis         = m_millis;
    g_bios.delay_ms       = m_delay;
    g_bios.display_width  = m_width;
    g_bios.display_height = m_height;
    g_bios.display_clear  = m_clear;
    g_bios.display_pixel  = m_pixel;
    g_bios.display_text   = m_text;
    g_bios.display_flush  = m_flush;
    g_bios.button_pressed = m_button;

    app_setup(&g_bios);
}

void loop() {
    app_loop(&g_bios);
}

#endif /* BIOS_HOST_M5 */
