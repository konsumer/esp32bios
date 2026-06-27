/*
 * compat.h -- host-facing hooks for the Flipper compatibility layer.
 *
 * A host firmware that wants to run Flipper apps:
 *   1. builds a BiosTable and calls compat_set_bios(&table);
 *   2. passes compat_resolve as the ELF loader's symbol resolver, so the loaded
 *      app's furi / gui / canvas imports bind to the shim.
 *
 * compat_resolve is this layer's "symbol table" -- the same role api_symbols.csv
 * plays in real Flipper firmware.
 */
#ifndef COMPAT_H
#define COMPAT_H

#include <stdint.h>
#include "bios.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Point the shim at the active BIOS before launching an app. */
void compat_set_bios(const BiosTable* bios);

/* Resolve a Flipper-API symbol name to the shim function's address, or 0. */
uintptr_t compat_resolve(const char* name);

#ifdef __cplusplus
}
#endif

#endif
