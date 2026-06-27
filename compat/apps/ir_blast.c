/*
 * ir_blast.c -- a Flipper Zero app that transmits an IR NEC code.
 *
 * Standard Flipper IR usage: build a list of mark/space durations, install a
 * "get data" callback the TX engine polls, and start async transmission. The
 * same source compiles for esp32bios; on a board with an IR LED (e.g. Cardputer)
 * it actually blasts the code.
 */
#include <furi.h>
#include <furi_hal_infrared.h>

/* walks a duration buffer for the TX engine; level alternates, starting on a mark */
typedef struct {
    const uint32_t* d;
    size_t n, i;
} TxState;

static FuriHalInfraredTxGetDataState ir_get_data(void* ctx, uint32_t* duration, bool* level) {
    TxState* s = ctx;
    *duration = s->d[s->i];
    *level = (s->i % 2 == 0);            /* even index = carrier on (mark) */
    s->i++;
    return (s->i >= s->n) ? FuriHalInfraredTxGetDataStateDone : FuriHalInfraredTxGetDataStateOk;
}

/* Encode an NEC frame (addr, cmd) into mark/space durations (microseconds). */
static size_t nec_encode(uint32_t* out, uint8_t addr, uint8_t cmd) {
    size_t n = 0;
    out[n++] = 9000; out[n++] = 4500;                /* leader */
    uint8_t bytes[4] = { addr, (uint8_t)~addr, cmd, (uint8_t)~cmd };
    for (int b = 0; b < 4; b++) {
        for (int bit = 0; bit < 8; bit++) {          /* LSB first */
            out[n++] = 560;                          /* bit mark */
            out[n++] = (bytes[b] & (1 << bit)) ? 1690 : 560;  /* 1 or 0 space */
        }
    }
    out[n++] = 560;                                  /* stop mark */
    return n;
}

int32_t ir_blast_app(void* p) {
    UNUSED(p);

    static uint32_t frame[80];
    size_t n = nec_encode(frame, 0x00, 0x16);        /* NEC addr 0x00, cmd 0x16 */

    TxState st = { frame, n, 0 };
    furi_hal_infrared_async_tx_set_data_isr_callback(ir_get_data, &st);
    furi_hal_infrared_async_tx_start(38000, 0.33f);  /* 38 kHz, 33% duty */
    furi_hal_infrared_async_tx_wait_termination();
    return 0;
}
