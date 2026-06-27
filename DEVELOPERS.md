# Developing apps for esp32bios

This is for **writing apps** that run on esp32bios. If you just want to flash
firmware to a device, see [README.md](README.md). For the internals, see
[INFO.md](INFO.md).

The deal: you write your app against one small header and it runs on **any**
esp32bios device — Cardputer, an OLED board, whatever — with no hardware-specific
code and, if you want, without anyone reflashing their firmware.

## Write your app

Your app lives in `src/app.cpp`. It includes exactly one thing — `include/bios.h`
— and talks to the hardware only through the `BiosTable` pointer it's handed:

```c
#include "bios.h"

void app_setup(const BiosTable* bios) {
    bios->log("hello");
}

void app_loop(const BiosTable* bios) {
    bios->display_clear(BIOS_BLACK);
    bios->display_text(6, 6, "hi there", BIOS_WHITE);
    bios->display_flush();
    bios->delay_ms(33);
}
```

The one rule: **only reach the hardware through that pointer**. No
`#include <Arduino.h>`, no driver libraries, no pin numbers. That's what keeps
your app portable.

## BIOS API reference

Everything you can call, via the `bios` pointer. `include/bios.h` is the source of
truth; this is the quick version.

| Call | Returns | What it does |
|------|---------|--------------|
| `log(const char* msg)` | — | Write a line to the debug/serial log. |
| `millis()` | `uint32_t` | Milliseconds since boot. |
| `delay_ms(uint32_t ms)` | — | Block for `ms` milliseconds. |
| `display_width()` | `int16_t` | Screen width in pixels. **Read it; don't assume a size.** |
| `display_height()` | `int16_t` | Screen height in pixels. |
| `display_clear(uint16_t color)` | — | Fill the whole screen with `color`. |
| `display_pixel(int16_t x, int16_t y, uint16_t color)` | — | Set one pixel. |
| `display_text(int16_t x, int16_t y, const char* s, uint16_t color)` | — | Draw a string at `(x,y)`. |
| `display_flush()` | — | Push what you've drawn to the panel. **Call once per frame, after drawing.** |
| `button_pressed(uint8_t id)` | `bool` | Is button `id` down right now? |

**Colors** are 16-bit RGB565. Predefined: `BIOS_BLACK`, `BIOS_WHITE`, `BIOS_RED`,
`BIOS_GREEN`, `BIOS_BLUE`. On mono displays anything non-black just lights the pixel.

**Buttons**: `BIOS_BTN_A`, `BIOS_BTN_B`, `BIOS_BTN_C`. Each host maps these to
whatever it physically has; a device with fewer buttons simply returns `false` for
the missing ones.

**Your two entry points** (you implement these; the firmware calls them):
`app_setup(const BiosTable* bios)` once at start, then `app_loop(const BiosTable* bios)`
repeatedly. A typical `app_loop` clears, draws, calls `display_flush()`, and
`delay_ms()`s to set the frame rate.

Before trusting the table you can check `bios->magic == BIOS_MAGIC` and
`bios->version == BIOS_VERSION` — `src/app.cpp` shows the pattern.

## Test it instantly, no hardware

```sh
pio run -e native -t exec        # runs your app on your computer, drawing as ASCII
```

Same `src/app.cpp`, no device required. This is the fastest edit→see-it loop.

## Get your app onto a device — two options

### A. Bundle it into the firmware

Compile your app into the firmware and flash the whole thing. Simplest, and works
on every device:

```sh
pio run -e cardputer -t upload        # use the env that matches the device
```

(The device→env table is in [README.md](README.md).) Downside: changing the app
means reflashing the firmware.

### B. Load it at runtime (no firmware reflash)

Install a BIOS firmware once, then ship just your app as a file the device loads
from its flash filesystem at boot. Update the app without touching the firmware.

```sh
# one time per device: install the loader firmware
pio run -e esp32-elf -t upload

# each time you change your app:
./loader/build_esp32_app.sh           # compiles src/app.cpp -> data/app.elf
pio run -e esp32-elf -t uploadfs      # uploads just the app to the device
pio run -e esp32-elf -t monitor       # watch it run
```

This is currently set up and verified for a **classic ESP32** (not the S3-based
Cardputer yet). The mechanism — a tiny on-device ELF loader — is explained in
[INFO.md](INFO.md), including the one step that still needs on-hardware
confirmation.

## Handing your app to end-users

Until there's a nicer distribution story, tell your users:

1. Which firmware to install — point them at the [README.md](README.md) table row
   for their device (or, for runtime loading, the `esp32-elf` firmware).
2. For runtime-loaded apps: give them your `data/app.elf` and the one command to
   push it, `pio run -e esp32-elf -t uploadfs`.

## Device support (firmware TODO list)

Each line is one firmware target = one `src/host_*.cpp` + one `[env:...]`. Checked
boxes exist today; unchecked are candidates to build. Most of the work collapses
into a few shared hosts (see the notes), so this is shorter than it looks.

### Done

- [x] **Desktop / native** — ASCII in the terminal, no hardware (`native`)
- [x] **Any ESP32, no display** — renders over USB serial (`esp32-serial`)
- [x] **ESP32 + SSD1306 OLED** 128×64 I²C (`esp32-ssd1306`)
- [x] **M5Stack Core / Core2 / Fire / StickC** — M5Unified (`m5stack`)
- [x] **M5Stack Cardputer** (ESP32-S3) — M5Unified (`cardputer`)
- [x] **Runtime ELF loader**, classic ESP32 — loads apps from flash (`esp32-elf`)

### M5 line — nearly free (reuse `host_m5.cpp`, just add an env)

The M5Unified host already drives the whole family; these are mostly a new
`[env:...]` with the right `board =` and auto-detection.

- [ ] **M5StickC / StickC Plus / Plus2**
- [ ] **M5 AtomS3 / AtomS3 Lite** (tiny 128×128 LCD)
- [ ] **M5Stack CoreS3 / CoreS3 SE**
- [ ] **M5Stack Core2 touch** (expose touch through the contract)

### Color TFT boards — one shared host (LovyanGFX or TFT_eSPI, configured per board)

A single `host_tft.cpp` parameterized by build flags covers most of these.

- [ ] **LilyGo / TTGO T-Display** (ST7789 240×135)
- [ ] **LilyGo T-Display-S3** (ST7789 320×170)
- [ ] **Cheap Yellow Display** (ESP32-2432S028, ILI9341 320×240 + touch)
- [ ] **Generic ESP32 + ST7789 / ILI9341** breakout
- [ ] **WT32-SC01 / Sunton** panels (ESP32-S3 + capacitive touch)

### Boards with input worth exposing (needs a contract addition)

These have keyboards/extra input — good reason to **append** a keyboard/touch
function to `BiosTable` (see "Extending the contract" in [INFO.md](INFO.md)).

- [ ] **LilyGo T-Deck** (ESP32-S3, trackball + keyboard + ST7789)
- [ ] **M5 Cardputer keyboard** (currently only button A is wired; add the keys)
- [ ] **ESP32-S3-BOX / BOX-3** (touch + mic)

### Other

- [ ] **Heltec WiFi Kit 32** (built-in SSD1306) — likely just new pins for `host_ssd1306`
- [ ] **SDL2 desktop host** — a real graphical window instead of ASCII (`native` v2)
- [ ] **Runtime ELF loader for ESP32-S3** — needs the S3 toolchain in
  `build_esp32_app.sh` + a cache flush after the IRAM copy (see [INFO.md](INFO.md))

## Adding a new device

To knock an item off the list, add a host: copy the closest `src/host_*.cpp`,
implement the dozen `BiosTable` functions for that hardware, give it a
`-DBIOS_HOST_xxx` flag, and add a matching `[env:...]` in `platformio.ini`. Your
app never changes. See [INFO.md](INFO.md) for a walkthrough of what a host does.
