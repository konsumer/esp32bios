/*
 * bios.h  --  The hardware-abstraction "contract" (a.k.a. the BIOS jump table).
 *
 * This is the ONLY thing an app links against. The app never touches a display
 * driver, a GPIO, or millis() directly -- it calls through the BiosTable that
 * the host firmware hands it. Swap the host, keep the app: no recompile needed.
 *
 * Pure C, so the same header works for native C, Arduino C++, an interpreter,
 * or a separately compiled binary.
 */
#ifndef BIOS_H
#define BIOS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Identity / ABI guard. An app reads these first to make sure the table it was
 * handed is real and that it understands the layout.  'B' 'I' 'O' 'S'. */
#define BIOS_MAGIC   0x534F4942u
#define BIOS_VERSION 1

/* Colors are 16-bit RGB565, the lingua franca of small color displays.
 * Mono displays just treat non-zero as "lit". */
#define BIOS_BLACK   0x0000
#define BIOS_WHITE   0xFFFF
#define BIOS_RED     0xF800
#define BIOS_GREEN   0x07E0
#define BIOS_BLUE    0x001F

/* Logical button ids. A host maps these to whatever it physically has. */
#define BIOS_BTN_A   0
#define BIOS_BTN_B   1
#define BIOS_BTN_C   2

/*
 * The jump table.
 *
 * RULES FOR EVOLVING THIS (so old apps keep running on new firmware):
 *   - Only ever APPEND new function pointers to the end.
 *   - Never reorder or remove existing entries.
 *   - Bump BIOS_VERSION and check `version`/`size` before calling a new entry.
 * That is the entire forward-compatibility story.
 */
typedef struct BiosTable {
    uint32_t magic;     /* == BIOS_MAGIC */
    uint16_t version;   /* == BIOS_VERSION the host was built with */
    uint16_t size;      /* sizeof(BiosTable); lets apps detect truncated tables */

    /* --- system / debug --- */
    void     (*log)(const char* msg);
    uint32_t (*millis)(void);
    void     (*delay_ms)(uint32_t ms);

    /* --- display --- */
    int16_t  (*display_width)(void);
    int16_t  (*display_height)(void);
    void     (*display_clear)(uint16_t color);
    void     (*display_pixel)(int16_t x, int16_t y, uint16_t color);
    void     (*display_text)(int16_t x, int16_t y, const char* s, uint16_t color);
    void     (*display_flush)(void);   /* push the back buffer to the panel */

    /* --- input --- */
    bool     (*button_pressed)(uint8_t button_id);
} BiosTable;

/*
 * App entry points. The PROGRAM implements these; the HOST calls them and
 * passes the table pointer in. This is the discovery mechanism -- no hardcoded
 * address required, and it works for native, Arduino, and loadable binaries.
 *
 * (If you truly want the "fixed memory location" flavor, the host can also
 *  publish `&table` into a known slot -- see README "Discovery options".)
 */
void app_setup(const BiosTable* bios);
void app_loop(const BiosTable* bios);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_H */
