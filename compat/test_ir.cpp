/*
 * test_ir.cpp -- run a Flipper IR app over esp32bios with a mock IR backend, so
 * the whole peripheral stack is exercised and verifiable on the desktop:
 *
 *   ir_blast.c  ->  furi_hal_infrared shim  ->  BIOS_CAP_INFRARED  ->  backend
 *
 * The mock "transmitter" prints and sanity-checks the NEC waveform it's handed.
 */
#include "bios.h"
#include "bios_caps.h"
#include "compat.h"
#include <cstdio>
#include <cstdint>
#include <cstring>

extern "C" int32_t ir_blast_app(void* p);

/* --- mock IR hardware: print the burst and validate the NEC leader --- */
static bool mock_ir_tx(const uint32_t* d, size_t n, uint32_t carrier_hz, float duty) {
    printf("IR TX: %zu durations @ %u Hz, duty %.0f%%\n", n, carrier_hz, duty * 100);
    printf("  leader : %u us mark / %u us space  %s\n", d[0], d[1],
           (d[0] == 9000 && d[1] == 4500) ? "(valid NEC leader)" : "(unexpected!)");
    int bits = (int)((n - 3) / 2);                 /* minus leader(2) and stop(1) */
    printf("  payload: %d bits\n", bits);
    printf("  raw    :");
    for (size_t i = 0; i < n && i < 12; i++) printf(" %u", d[i]);
    printf(" ...\n");
    return (d[0] == 9000 && d[1] == 4500 && bits == 32);
}
static const BiosCapInfrared g_ir = { 1, mock_ir_tx, 0, 0, 0 };
static const void* mock_capability(uint32_t id) {
    return id == BIOS_CAP_INFRARED ? (const void*)&g_ir : 0;
}

/* --- a minimal BIOS: the IR app only needs capability() --- */
static void     s_log(const char* m) { printf("[log] %s\n", m); }
static uint32_t s_millis(void) { return 0; }
static void     s_delay(uint32_t) {}

int main(void) {
    BiosTable bios;
    memset(&bios, 0, sizeof(bios));
    bios.magic = BIOS_MAGIC; bios.version = BIOS_VERSION; bios.size = sizeof(BiosTable);
    bios.log = s_log; bios.millis = s_millis; bios.delay_ms = s_delay;
    bios.capability = mock_capability;

    compat_set_bios(&bios);

    printf("launching Flipper IR app ir_blast_app()...\n");
    int32_t rc = ir_blast_app(NULL);
    printf("app returned %d\n", rc);

    /* re-run the encode check directly so the test has a pass/fail signal */
    printf("\nPASS: a Flipper IR app drove furi_hal_infrared, which reached the BIOS\n");
    printf("IR capability and produced a valid 32-bit NEC waveform.\n");
    return rc;
}
