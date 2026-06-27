# Flipper-app compatibility (proof of concept)

Goal: **let people build a Flipper Zero app from source against esp32bios**, so you
inherit Flipper's app ecosystem instead of starting an empty one. This directory
is a working PoC of that idea.

It is **not** drop-in `.fap` compatibility — a `.fap` is compiled ARM machine code
and the ESP32 is Xtensa, so prebuilt binaries can't run. This is *source*
compatibility: the same C source recompiles for ESP32 and runs.

## What's here

| File | Role |
|------|------|
| `apps/hello_world.c` | A standard Flipper app (ViewPort + draw/input callbacks + event loop). Knows nothing about esp32bios. |
| `include/` | Flipper-API headers (`furi.h`, `gui/gui.h`, `gui/canvas.h`, `gui/view_port.h`, `input/input.h`) — re-declared to match Flipper's signatures. |
| `furi_compat.cpp` | The shim: implements that API over the esp32bios `BiosTable`. This lives in the host firmware. |
| `host_native_fap.cpp` | Desktop host that launches the app over the shim (ASCII screen). |

```
  apps/hello_world.c   (Flipper API: furi_*, gui_*, canvas_*)
          |
          v
  furi_compat.cpp      (the shim)
          |
          v
  BiosTable            (esp32bios contract)
          |
          v
  any host (native, Cardputer, OLED, …)
```

## Run it

```sh
make run        # builds + runs the Flipper app on your computer (ASCII, 128x64)
```

You'll see the app draw a framed "Hello World" / "Back to exit" screen and then
exit cleanly when a Back press is simulated — the app's real event loop running
unmodified over the BIOS.

```sh
make xtensa     # cross-compiles the app + shim for ESP32 and lists the app's imports
```

## How it maps to Flipper's real architecture

Flipper apps don't carry their dependencies — they import the firmware's API by
name, and the app loader resolves those names against a symbol table. Our pipeline
is the same:

- The app compiles to a relocatable object whose `furi_*` / `gui_*` / `canvas_*`
  calls are **undefined symbols** (confirmed by `make xtensa`).
- On device, the ELF loader (`../src/elfload.cpp`) resolves them against the host —
  the shim functions are the host's "symbol table". This is exactly the
  `e_resolve()` path in `../src/host_elf.cpp`, which the bundled `app.cpp` never
  exercised because it had zero imports; a real Flipper app exercises it fully.

The one genuinely different thing is the execution model, handled in
`furi_compat.cpp`: Flipper apps own their main loop and block in
`furi_message_queue_get()` while a separate GUI thread renders and feeds input. We
collapse that to one thread by running the render+input "pump" *inside*
`furi_message_queue_get()`. To the app it's indistinguishable from the real thing.

## Run it on real hardware

The shim is wired into a firmware host (`../src/host_flipper.cpp`) that loads a
Flipper app from the flash filesystem and runs it. Two targets:

```sh
# classic ESP32 (renders the app over the serial monitor):
loader/build_flipper_app.sh esp32
pio run -e esp32-flipper -t upload -t uploadfs -t monitor

# M5Stack Cardputer (renders on the screen):
loader/build_flipper_app.sh s3
pio run -e cardputer-flipper -t upload -t uploadfs -t monitor
```

The host builds a BiosTable, loads `/app.elf`, resolves its furi/gui/canvas
imports via `compat_resolve` (the shim's symbol table — see `furi_compat.cpp`,
the hand-written analog of Flipper's `api_symbols.csv`), points the shim at the
BIOS, and calls the FAP entry point.

## What's verified

- ✅ A standard Flipper app compiles **unchanged in shape** against the shim.
- ✅ It **runs** end-to-end on the desktop host (draws, takes input, exits cleanly).
- ✅ App **and** shim **cross-compile for ESP32** (classic LX6 **and** S3 LX7).
- ✅ The real Xtensa FAP object **loads through the on-device ELF loader**, with
  **every furi/gui/canvas import resolved against the shim** and the FAP entry
  located (`test_fap_load.cpp`). This added two Xtensa relocation types the loader
  now handles (`ASM_EXPAND`/`ASM_SIMPLIFY`) that direct external calls produce.
- ✅ Both firmwares (`esp32-flipper`, `cardputer-flipper`) **compile and build a
  flashable image**.

Not yet confirmed (needs a board): the final step of *executing* the loaded app on
the chip. Same caveat as any loaded code — see the cache note in `../INFO.md`.

## Peripherals: IR (`furi_hal_infrared`)

Beyond the GUI, the shim covers a real peripheral: Flipper's IR HAL. The header
`include/furi_hal_infrared.h` matches Flipper's `furi_hal_infrared.h` (the async
TX get-data ISR callback model, RX capture callbacks, the enums), and
`furi_compat.cpp` implements it over the BIOS `BIOS_CAP_INFRARED` capability
(`../include/bios_caps.h`). A board that has an IR LED advertises the capability;
one that doesn't returns NULL and IR apps no-op instead of crashing.

```sh
make ir        # runs a Flipper IR app (apps/ir_blast.c) over a mock IR backend,
               # printing the NEC waveform it would transmit
```

- ✅ A Flipper IR app (`apps/ir_blast.c`, standard `furi_hal_infrared` usage)
  drives the shim, reaches the BIOS IR capability, and emits a **valid 32-bit NEC
  waveform** — verified on the desktop with a mock transmitter.
- ✅ It **cross-compiles for Xtensa** and **loads through the real loader** with
  its `furi_hal_infrared` imports resolved against the shim.
- ✅ The `cardputer-flipper` firmware provides a real IR backend (bit-banged 38 kHz
  carrier on the Cardputer's IR LED, GPIO44) and compiles.

This is the model for every other radio: add a `BIOS_CAP_*` sub-table in
`../include/bios_caps.h`, implement the matching `furi_hal_*` in the shim on top of
it, and have boards advertise the capability when they have the hardware. IR is
done because the Cardputer actually has the LED; sub-GHz/NFC would be the same
shape but can't be tested without the radios.

## Honest scope

- This covers the `furi` + `gui`/`canvas` + `input` surface a GUI app needs. Full
  source compatibility means growing the shim toward Flipper's whole API (storage,
  notifications, dialogs, the view/view_dispatcher framework, FuriString, …). It's
  additive — each app needs only the slice it calls (`make xtensa` shows you which).
- Many popular Flipper apps need **radios the Cardputer doesn't have** (sub-GHz,
  NFC, 125kHz RFID, iButton). Those can't work regardless of the shim. The
  realistically portable set is games, tools, IR, GPIO, and UI apps.
