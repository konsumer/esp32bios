/*
 * elfload.h -- a minimal relocatable-ELF (ET_REL) loader for the BIOS.
 *
 * It loads a *relocatable object* (a .o / ET_REL ELF) built from an app, places
 * its sections into runtime memory, applies relocations, resolves any external
 * symbols against the host, and hands back the app's entry points. This is the
 * on-device equivalent of the desktop dlopen() loader in loader/.
 *
 * Why ET_REL and not a shared object: a .o already carries exactly what a tiny
 * loader needs -- sections, a symbol table, and relocations -- with none of the
 * dynamic-linking machinery. Build the app with:
 *
 *     xtensa-esp32-elf-g++ -c -mlongcalls -mtext-section-literals \
 *         -fno-rtti -fno-exceptions -fno-stack-protector -Iinclude \
 *         src/app.cpp -o app.elf
 *
 * The loader itself is portable C++: all platform specifics (executable memory,
 * cache, symbol resolution, logging) are injected via elf_env callbacks, so the
 * same code is unit-tested on the desktop and runs on the ESP32.
 */
#ifndef ELFLOAD_H
#define ELFLOAD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host-provided services. Any of log/resolve may be NULL. */
typedef struct elf_env {
    /* Allocate memory the CPU can execute from (IRAM on ESP32). Must be at least
     * 4-byte aligned. */
    void*    (*alloc_exec)(size_t size);
    /* Allocate normal byte-addressable RAM for .data/.rodata/.bss. */
    void*    (*alloc_data)(size_t size);
    /* Copy into an alloc_exec() region. On targets where instruction RAM only
     * allows 32-bit access (ESP32 IRAM), this does word-safe copies; on the
     * desktop it is just memcpy. */
    void     (*copy_exec)(void* dst, const void* src, size_t size);
    /* Resolve an undefined symbol by name to its address, or return 0. Used for
     * stragglers like memcpy/memset the compiler may emit. The BIOS table is NOT
     * resolved here -- it is passed to the app as an argument. */
    uintptr_t (*resolve)(const char* name);
    /* Optional diagnostics. */
    void     (*log)(const char* msg);
} elf_env;

/* A named entry point the caller wants located after load. The caller fills
 * `name`; the loader fills `addr` with its runtime address (0 if not found).
 * The entry's real signature is the caller's business -- cast `addr` to it.
 * This keeps the loader ABI-agnostic: an esp32bios app asks for
 * "app_setup"/"app_loop", a Flipper app asks for its FAP entry. */
typedef struct elf_symbol {
    const char* name;
    void*       addr;
} elf_symbol;

/* Return codes. */
#define ELF_OK                 0
#define ELF_ERR_MAGIC         -1   /* not an ELF / wrong class / wrong type   */
#define ELF_ERR_MACHINE       -2   /* not the expected machine                */
#define ELF_ERR_NO_SYMTAB     -3
#define ELF_ERR_ALLOC         -4
#define ELF_ERR_RELOC         -5   /* unsupported relocation type             */
#define ELF_ERR_UNRESOLVED    -6   /* undefined symbol the host didn't supply */
#define ELF_ERR_NO_ENTRY      -7   /* a requested entry symbol was not found  */

/*
 * Load `image` (length `len`), place + relocate it, resolve undefined symbols
 * via env->resolve, and look up each requested entry in `entries[0..n_entries)`.
 * `expect_machine` is the ELF e_machine to require (EM_XTENSA on device); pass 0
 * to skip the machine check (used by the desktop structural test).
 * Returns ELF_OK only if every requested entry was found.
 */
int elf_load(const uint8_t* image, size_t len, const elf_env* env,
             uint16_t expect_machine, elf_symbol* entries, int n_entries);

/*
 * Read-only walk that logs sections, symbols, and the relocation TYPES present,
 * without allocating or modifying anything. Invaluable for bringing the loader
 * up on a new object: run it once and see exactly which reloc types you must
 * handle. Returns ELF_OK or an ELF_ERR_* code.
 */
int elf_inspect(const uint8_t* image, size_t len, const elf_env* env);

#ifdef __cplusplus
}
#endif

#endif /* ELFLOAD_H */
