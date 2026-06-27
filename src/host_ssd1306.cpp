/*
 * host_ssd1306.cpp -- host for a 128x64 I2C OLED (very common, ~$3).
 *
 * Real pixels and real text on a real panel. Here the BIOS primitives map
 * straight onto Adafruit_GFX, which is the point: each host translates the
 * SAME contract to whatever its hardware library wants.
 *
 * Wiring (default I2C): SDA->GPIO21, SCL->GPIO22, addr 0x3C. Button A on GPIO0.
 */
#ifdef BIOS_HOST_SSD1306

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "bios.h"

#define OLED_W 128
#define OLED_H 64
#define OLED_ADDR 0x3C
#define BTN_A_PIN 0

static Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
static BiosTable g_bios;

/* RGB565 -> mono: anything non-black lights the pixel. */
static inline uint16_t mono(uint16_t c) { return c ? SSD1306_WHITE : SSD1306_BLACK; }

static void     o_log(const char* m)   { Serial.print("[log] "); Serial.println(m); }
static uint32_t o_millis(void)         { return (uint32_t)millis(); }
static void     o_delay(uint32_t ms)   { delay(ms); }
static int16_t  o_width(void)          { return OLED_W; }
static int16_t  o_height(void)         { return OLED_H; }
static void     o_clear(uint16_t c)    { oled.fillScreen(mono(c)); }
static void     o_pixel(int16_t x, int16_t y, uint16_t c) { oled.drawPixel(x, y, mono(c)); }
static void     o_text(int16_t x, int16_t y, const char* s, uint16_t c) {
    oled.setTextColor(mono(c));
    oled.setTextSize(1);
    oled.setCursor(x, y);
    oled.print(s);
}
static void     o_flush(void)          { oled.display(); }
static bool     o_button(uint8_t id)   { return id == BIOS_BTN_A && digitalRead(BTN_A_PIN) == LOW; }

void setup() {
    Serial.begin(115200);
    pinMode(BTN_A_PIN, INPUT_PULLUP);
    Wire.begin();
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("SSD1306 not found!");
        for (;;) delay(1000);
    }
    oled.clearDisplay();
    oled.display();

    g_bios.magic          = BIOS_MAGIC;
    g_bios.version        = BIOS_VERSION;
    g_bios.size           = sizeof(BiosTable);
    g_bios.log            = o_log;
    g_bios.millis         = o_millis;
    g_bios.delay_ms       = o_delay;
    g_bios.display_width  = o_width;
    g_bios.display_height = o_height;
    g_bios.display_clear  = o_clear;
    g_bios.display_pixel  = o_pixel;
    g_bios.display_text   = o_text;
    g_bios.display_flush  = o_flush;
    g_bios.button_pressed = o_button;
    g_bios.capability     = bios_no_caps;

    app_setup(&g_bios);
}

void loop() {
    app_loop(&g_bios);
}

#endif /* BIOS_HOST_SSD1306 */
