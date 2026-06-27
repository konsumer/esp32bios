/*
 * bios_vector.cpp -- implementation of the published-pointer slot.
 *
 * Two backends, picked at compile time:
 *
 *   ESP32   : the slot lives in RTC slow memory via RTC_NOINIT_ATTR. The linker
 *             reserves the storage and keeps it out of normal RAM/BSS, so it is
 *             a genuine fixed, persistent location -- the honest version of
 *             "look it up at a known address". If you want a *literal* hardcoded
 *             address instead, see the commented BIOS_VECTOR_FIXED_ADDR block.
 *
 *   native  : a process-global, so the desktop loader demo exercises the exact
 *             same publish/discover API.
 *
 * Note this file is compiled into the HOST only. A separately built/loaded app
 * does NOT compile it; it just calls bios_vector_discover(), and that symbol is
 * resolved against the host at load time (dlopen on desktop, the ELF loader's
 * symbol table on device). That is how a guest with zero link-time knowledge of
 * the host still finds the BIOS.
 */
#include "bios_vector.h"

#if defined(ARDUINO_ARCH_ESP32)
  #include <esp_attr.h>

  /* Reserved, persistent slot. magic gates whether ptr is trustworthy. */
  static RTC_NOINIT_ATTR uint32_t           s_magic;
  static RTC_NOINIT_ATTR const BiosTable*    s_ptr;

  /* --- Optional: truly fixed/hardcoded address flavor -----------------------
   * RTC slow memory is 0x50000000..0x50001FFF on the classic ESP32. If you
   * insist on a compile-time constant address that a separate binary can bake
   * in, carve a couple of words out of the top of that range and make sure
   * nothing else uses them (don't enable both forms at once):
   *
   *   #define BIOS_VECTOR_FIXED_ADDR 0x50001FF0u
   *   #define s_magic (*(volatile uint32_t*)(BIOS_VECTOR_FIXED_ADDR + 0))
   *   #define s_ptr   (*(const BiosTable**)(BIOS_VECTOR_FIXED_ADDR + 4))
   *
   * The RTC_NOINIT_ATTR form above is preferred: same persistence, but the
   * linker guarantees nothing else lands on those words.
   * ------------------------------------------------------------------------- */

#else
  /* Desktop / generic: a plain process-global is enough. */
  static uint32_t        s_magic;
  static const BiosTable* s_ptr;
#endif

void bios_vector_publish(const BiosTable* table)
{
    s_ptr   = table;
    s_magic = BIOS_VECTOR_MAGIC;
}

const BiosTable* bios_vector_discover(void)
{
    if (s_magic != BIOS_VECTOR_MAGIC) return 0;          /* never published */
    const BiosTable* t = s_ptr;
    if (!t) return 0;
    if (t->magic != BIOS_MAGIC) return 0;                /* slot points at junk */
    if (t->version != BIOS_VERSION) return 0;            /* ABI mismatch */
    return t;
}
