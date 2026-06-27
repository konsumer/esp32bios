/*
 * bios_vector.h -- the "published pointer" discovery mechanism.
 *
 * This is the safe version of the "find the BIOS at a fixed memory location"
 * idea. Instead of assuming the whole jump table lives at a hardcoded address
 * (it can't on ESP32 -- see README), the host parks ONE pointer-sized value in
 * a small, known, persistent slot, guarded by a magic number. Any app -- even a
 * separately compiled binary that was never linked against the host -- can read
 * that slot and recover the table.
 *
 *   Host at boot:   bios_vector_publish(&my_table);
 *   App later:      const BiosTable* bios = bios_vector_discover();
 *                   if (bios) bios->display_clear(BIOS_BLACK);
 *
 * On ESP32 the slot lives in RTC slow memory (survives deep sleep, fixed across
 * boots). On desktop it's a process-global, so the loader demo can prove the
 * same flow end to end.
 */
#ifndef BIOS_VECTOR_H
#define BIOS_VECTOR_H

#include "bios.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stamped into the slot so a stale or never-initialized slot reads as empty
 * instead of handing back a garbage pointer. 'B','V','C','1'. */
#define BIOS_VECTOR_MAGIC 0x31435642u

/* Host calls once after it has built its table. */
void bios_vector_publish(const BiosTable* table);

/* App/loaded code calls to recover the table. Returns NULL if nothing valid
 * has been published (bad magic, bad table magic, or version mismatch). */
const BiosTable* bios_vector_discover(void);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_VECTOR_H */
