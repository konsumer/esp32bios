/*
 * bios_caps.h -- optional peripheral capabilities for the BIOS.
 *
 * The core BiosTable holds what *every* device has (screen, buttons, time).
 * Peripherals -- IR, sub-GHz, NFC, ... -- are optional: present on some boards,
 * absent on others. So they are not jammed into BiosTable. Instead BiosTable has
 * one accessor:
 *
 *     const void* cap = bios->capability(BIOS_CAP_INFRARED);
 *     if (cap) { const BiosCapInfrared* ir = (const BiosCapInfrared*)cap; ... }
 *
 * A host returns a pointer to the capability's sub-table if it has the hardware,
 * or NULL if it doesn't. Each capability carries its own `version` so it can grow
 * append-only, independently of the core ABI. This is esp32bios's answer to
 * Flipper's furi_hal_* layer -- the same idea, made discoverable and optional.
 */
#ifndef BIOS_CAPS_H
#define BIOS_CAPS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capability ids (ASCII mnemonics, easy to spot in a memory dump). */
#define BIOS_CAP_INFRARED 0x00495200u  /* 'IR'  */
#define BIOS_CAP_SUBGHZ   0x53474800u  /* 'SGH' (future) */
#define BIOS_CAP_NFC      0x4E464300u  /* 'NFC' (future) */

/*
 * Infrared. Modeled on Flipper's furi_hal_infrared: transmit is a sequence of
 * carrier on/off durations in microseconds (a "mark/space" burst pair list)
 * modulated at a carrier frequency; receive is a stream of captured edges.
 */
typedef void (*BiosIrRxCallback)(void* ctx, bool level, uint32_t duration_us);

typedef struct BiosCapInfrared {
    uint32_t version;

    /* Transmit `count` durations (microseconds), alternating carrier-ON (mark)
     * then carrier-OFF (space), beginning with a mark, modulated at `carrier_hz`
     * with `duty` (0..1). Blocks until the burst is sent. Returns false if it
     * couldn't transmit. */
    bool (*tx)(const uint32_t* durations_us, size_t count, uint32_t carrier_hz, float duty);

    /* Begin/stop capturing raw IR edges; `cb` is called per edge. Either may be
     * NULL if the board can transmit but not receive. */
    void (*rx_start)(BiosIrRxCallback cb, void* ctx, uint32_t timeout_us);
    void (*rx_stop)(void);

    /* True while a transmit is in progress (always false for blocking tx). */
    bool (*is_busy)(void);
} BiosCapInfrared;

#ifdef __cplusplus
}
#endif

#endif /* BIOS_CAPS_H */
