# esp32bios — how it works

The design, the rationale, and what's been verified — a deep dive for anyone who
wants to understand or extend the internals. To **install firmware** see
[README.md](README.md); to **write apps** see [DEVELOPERS.md](DEVELOPERS.md).

## The idea

A **contract** — a struct of function pointers (the "jump table") — lets a single
program run on different hardware without recompiling for each board. The program
calls things like `display_pixel(...)` through the table; each board's *host
firmware* fills the table with its own driver code.

```
        app.cpp  (the program — knows ONLY bios.h)
           |
           |  calls through
           v
   +-------------------+
   |    BiosTable      |   <- the contract (include/bios.h)
   +-------------------+
       ^     ^     ^
       |     |     |        each host implements the same contract
  host_serial  host_ssd1306  host_m5 ...
```

## Files

| File | Role |
|------|------|
| `include/bios.h`       | The contract: the `BiosTable` jump table + app entry points. **The only thing the app depends on.** |
| `src/app.cpp`          | The program. No board code, no `#include <Arduino.h>`. Bouncing pixel + text + button. |
| `src/host_native.cpp`  | Desktop host — renders to the terminal as ASCII. No hardware needed. |
| `src/host_serial.cpp`  | Any ESP32, zero libraries — renders over the serial monitor. |
| `src/host_ssd1306.cpp` | ESP32 + 128×64 I²C OLED (Adafruit GFX). |
| `src/host_m5.cpp`      | M5Stack family incl. Cardputer (M5Unified) — color TFT + buttons. |
| `src/host_elf.cpp`     | ESP32 host that loads a separate app from LittleFS at runtime (no reflash). |
| `include/bios_vector.h`, `src/bios_vector.cpp` | Published-pointer discovery (the safe "fixed location"). |
| `include/elfload.h`, `src/elfload.cpp` | Minimal relocatable-ELF (ET_REL) loader for on-device app loading. |
| `loader/`              | Desktop separate-binary demo (`dlopen`) + the on-device ELF loader test & build script. |
| `examples/vector_demo.cpp` | Standalone proof of the publish/discover flow. |

Each `host_*.cpp` is wrapped in an `#ifdef`, so PlatformIO can compile all of them
every build and only the selected one (chosen by the env's `-DBIOS_HOST_xxx` flag)
produces code.

## Writing a host

A host implements the dozen functions in `BiosTable` against its hardware, fills
the struct, and calls `app_setup(&table)` then `app_loop(&table)` in a loop. Look
at `host_ssd1306.cpp` for the shortest real example — the BIOS primitives map
almost one-to-one onto Adafruit_GFX.

## Extending the contract

Only **append** new function pointers to the end of `BiosTable`, bump
`BIOS_VERSION`, and have apps check `version`/`size` before calling a new entry.
Never reorder or remove entries — that's the whole forward-compatibility rule. The
`magic`/`version`/`size` header fields let an app validate a table before trusting
it, so a stale binary fails loudly instead of jumping into garbage.

## How the app finds the table (discovery)

A common first instinct (and one an LLM suggested for this project) is to hardcode
the table's address: `#define M5_BIOS ((M5OS_JumpTable*)0x3F400000)`. **That
address won't work on ESP32**: `0x3F400000` is the memory-mapped SPI-flash (DROM)
region — it's *read-only*, you can't put a live struct there. And a fixed *RAM*
address is unreliable too, because the linker hands that RAM out to ordinary
variables unless you reserve it with a custom linker section.

Three realistic approaches, cleanest first:

### 1. Dependency injection (used by every host here)

The host calls `app_setup(&table)` / `app_loop(&table)` and passes the pointer in.
Zero magic addresses; works for native, Arduino, and loadable binaries. Recommended.

### 2. Published pointer at a known slot (`bios_vector.h`)

The safe version of "look up the BIOS at a fixed location". The host parks one
pointer-sized value in a small, known, persistent slot guarded by a magic number;
any app reads it back — no assumption that a whole struct lives at a hardcoded
address.

```c
host:  bios_vector_publish(&my_table);
app:   const BiosTable* bios = bios_vector_discover();   // NULL if none/ABI mismatch
```

- **ESP32**: the slot is `RTC_NOINIT_ATTR` storage in RTC slow memory — the linker
  reserves it and it survives deep sleep. `src/bios_vector.cpp` also shows the
  *literal hardcoded address* flavor (carving words out of `0x50001FF0`) if you
  truly want a compile-time constant a separate binary can bake in.
- **desktop**: a process-global, so the flow is testable with no hardware.

Standalone proof (guest finds the BIOS with nothing passed in):

```sh
g++ -std=c++17 -Iinclude examples/vector_demo.cpp src/bios_vector.cpp -o /tmp/vdemo && /tmp/vdemo
```

It prints a clean failure before the host publishes, then a successful discovery
after — including the magic/version validation that stops a stale slot from
handing back a garbage pointer.

### 3. A truly separate app binary

The app is compiled and linked completely independently from the host, and the
host loads it at runtime. Two demos: a desktop one with `dlopen`, and the real
on-device ELF loader.

## Separate binary on the desktop (`loader/`)

A complete, runnable demo where the host and the app are compiled and linked
**independently** and the host loads the app at runtime — the desktop equivalent
of loading an app off flash without reflashing firmware.

```sh
cd loader && make run      # builds host_loader + app.so separately, runs 90 frames
```

What it proves:
- `app.so` is built on its own (`-shared -fPIC`) and **never names the host**.
- `host_loader` is built on its own (`-rdynamic`) and **never names the app** — it
  takes the path on the command line, `dlopen`s it, and `dlsym`s `guest_setup` /
  `guest_loop` by name.
- The app finds the BIOS via `bios_vector_discover()`, whose symbol is resolved
  against the host at load time. That symbol resolution is exactly the job an
  on-device ELF loader's symbol table does.

## Separate binary on the ESP32 (`esp32-elf` env + `elfload.cpp`)

`dlopen` is desktop-only, so for the device there's a small ET_REL loader in
`src/elfload.cpp` (contract in `include/elfload.h`) driven by `src/host_elf.cpp`.
It loads `data/app.elf` from LittleFS at runtime — no firmware reflash to change
the app.

```sh
./loader/build_esp32_app.sh            # compile src/app.cpp -> data/app.elf (Xtensa ET_REL)
pio run -e esp32-elf -t upload         # the BIOS firmware (one time)
pio run -e esp32-elf -t uploadfs       # the app, into the device filesystem
pio run -e esp32-elf -t monitor
```

How it works:

- The app is built as a **relocatable object** (a `.o`, `ET_REL`) — it already
  carries sections + a symbol table + relocations, all a tiny loader needs, with
  none of the dynamic-linking machinery.
- The loader places each `SHF_ALLOC` section into runtime memory: executable
  sections into **IRAM** (`heap_caps_malloc(MALLOC_CAP_EXEC)`, copied with 32-bit
  word stores because IRAM is word-access-only), the rest into byte-addressable
  **DRAM**. It applies relocations, resolves any leftover externals against a host
  symbol table, finds `app_setup`/`app_loop`, and calls them with the BIOS table.
- The build flags in `build_esp32_app.sh` are chosen so the only relocation that
  needs real work is `R_XTENSA_32` (absolute addresses in literal pools).
  `-mtext-section-literals` keeps the literal pool inside `.text`, so the
  `l32r`/branch `R_XTENSA_SLOT0_OP` relocations are intra-section — already encoded
  by the assembler and preserved because the loader copies `.text` as one
  contiguous block. The loader enforces that rule: a *cross-section* SLOT0_OP is
  rejected with a clear message rather than silently miscompiled.

### But isn't the ESP32 Harvard — you can't run dynamic code?

Half true, and the false half is what makes this work. The ESP32 (Xtensa LX6) is a
*modified* Harvard machine. The real constraints are: (1) the CPU fetches
instructions only through addresses in the **instruction-bus window** ("IRAM"), not
the data-only window; and (2) that IRAM is **32-bit-access-only** — no byte
loads/stores.

What that does *not* forbid is loading code at runtime, because internal **SRAM1 is
dual-mapped**: the same physical RAM appears at both an instruction-window address
*and* a data-window address. You write code through the data address and execute it
through the instruction address — still Harvard, just the two doors into one room.
ESP-IDF hands you exactly this kind of RAM via `heap_caps_malloc(MALLOC_CAP_EXEC)`.

That maps straight onto the loader:

- `.text` → `MALLOC_CAP_EXEC` RAM (instruction window), copied with **32-bit word
  stores** (constraint 2).
- `.rodata`/`.bss` → ordinary byte-addressable **DRAM**. The code reaches a string
  by `l32r`-loading its absolute DRAM address from a literal (a 32-bit code-space
  read, which is allowed) and passing that pointer to the BIOS — it never byte-reads
  its own constants out of the instruction window.
- Calling the app is a function-pointer call into IRAM. The app in turn calls BIOS
  functions that live in the **firmware running from flash** (mapped into the
  instruction window by the flash cache). An indirect call from IRAM-code into
  flash-code is a normal cross-region call. App in IRAM, firmware in flash, data in
  DRAM — every access goes through the correct bus.

We also dodge the two genuinely hard parts: no position-independent-code gymnastics
(we patch absolute addresses in a *relocatable* object at load time), and no
flash-MMU juggling (we copy into IRAM rather than mapping the app's flash region as
executable in place). The classic alternative — a bytecode interpreter like
MicroPython or wasm3 — avoids native loading entirely at the cost of speed; this
runs real native code, which is why the Harvard question comes up at all.

### What's verified vs. not

The loader was run against a **real Xtensa object** built with the exact flags
above. `loader/test_elfload.cpp` confirms header parsing, section placement, the
`R_XTENSA_32` relocation arithmetic, the SLOT0_OP intra-section rule,
external-symbol resolution, and entry-point discovery all succeed (it can't
*execute* Xtensa code on a PC). The `esp32-elf` firmware and the LittleFS image
both compile/link cleanly.

The final **jump-into-IRAM** step can only be confirmed on a board. If it
misbehaves, the first suspect is instruction-cache coherency after the copy:
classic ESP32 (LX6) IRAM needs no flush; **ESP32-S3** (e.g. the Cardputer) and
flash-execute setups may. `build_esp32_app.sh` also targets the classic-ESP32
toolchain, so test the loader on a **classic ESP32** first; an S3 needs the S3
toolchain and possibly a cache flush.

### Debugging a new app object

```sh
./loader/build_esp32_app.sh
g++ -std=c++17 -Iinclude loader/test_elfload.cpp src/elfload.cpp -o /tmp/t && /tmp/t data/app.elf
```

`elf_inspect()` prints the section list, entry symbols, and the set of relocation
types present — run it the moment a new app object won't load and it tells you
precisely what the loader would need to handle.

## Testing notes for real hardware

- **Cardputer (ESP32-S3):** best for confirming the BIOS concept end-to-end on a
  real color screen — `pio run -e cardputer -t upload -t monitor`. M5Unified
  auto-detects the panel; the demo app adapts to its 240×135 resolution.
- **A classic ESP32 (e.g. DevKitC):** best for the runtime ELF loader (`esp32-elf`),
  since the loader's IRAM/no-cache-flush assumptions hold there.
- **Any ESP32:** `esp32-serial` renders the demo as ASCII over the serial monitor
  with zero extra libraries — a good first bring-up.
