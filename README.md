# esp32bios

**esp32bios** is firmware that turns an ESP32 device into a little machine that
can run apps. It provides the basics — screen, buttons, timing — through a fixed
"BIOS" interface, so an app written for esp32bios runs on any device that has it,
without being rebuilt for your specific hardware.

This page is for **installing the firmware on your device**. If you want to write
apps, see **[DEVELOPERS.md](DEVELOPERS.md)**. For how it all works under the hood,
see **[INFO.md](INFO.md)**.

## Install the firmware

> A web flasher (flash from your browser, no tools to install) is planned. Until
> then, use [PlatformIO](https://platformio.org/install): install it, open this
> folder in a terminal, plug in your device, and run the command for it.

| Your device | Command |
|-------------|---------|
| M5Stack **Cardputer** | `pio run -e cardputer -t upload` |
| Other **M5Stack** (Core / Core2 / Fire / StickC) | `pio run -e m5stack -t upload` |
| ESP32 with a **128×64 OLED** (SSD1306, I²C) | `pio run -e esp32-ssd1306 -t upload` |
| **Any other ESP32** (output shown over USB serial) | `pio run -e esp32-serial -t upload` |

Don't see your device? It can be added — point whoever's developing for you at
[DEVELOPERS.md](DEVELOPERS.md).

## Check that it worked

The firmware ships with a small built-in demo so you can confirm your screen and
buttons work: you'll see a bordered box, the text **ESP32 BIOS**, and a dot
bouncing around. To watch the serial output too, add `-t monitor`:

```sh
pio run -e cardputer -t upload -t monitor      # (use your device's env name)
```

## Try it with no hardware

Curious what it does without a device? Run the demo on your computer:

```sh
pio run -e native -t exec        # draws in the terminal as ASCII
```
