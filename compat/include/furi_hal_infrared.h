/*
 * furi_hal_infrared.h -- Flipper IR HAL (shim). Declarations match Flipper's real
 * furi_hal_infrared.h so IR app source compiles unchanged; implemented over the
 * esp32bios BIOS_CAP_INFRARED capability.
 */
#ifndef COMPAT_FURI_HAL_INFRARED_H
#define COMPAT_FURI_HAL_INFRARED_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FuriHalInfraredTxGetDataStateOk,        /* this sample is valid, keep going */
    FuriHalInfraredTxGetDataStateDone,      /* this sample is valid and the last */
    FuriHalInfraredTxGetDataStateLastDone,  /* no sample; transmission is over */
} FuriHalInfraredTxGetDataState;

typedef enum {
    FuriHalInfraredTxPinInternal,
    FuriHalInfraredTxPinExtPA7,
    FuriHalInfraredTxPinMax,
} FuriHalInfraredTxPin;

typedef FuriHalInfraredTxGetDataState (*FuriHalInfraredTxGetDataISRCallback)(
    void* context, uint32_t* duration, bool* level);
typedef void (*FuriHalInfraredTxSignalSentISRCallback)(void* context);
typedef void (*FuriHalInfraredRxCaptureCallback)(void* ctx, bool level, uint32_t duration);
typedef void (*FuriHalInfraredRxTimeoutCallback)(void* ctx);

/* --- async TX --- */
void furi_hal_infrared_async_tx_set_data_isr_callback(
    FuriHalInfraredTxGetDataISRCallback callback, void* context);
void furi_hal_infrared_async_tx_set_signal_sent_isr_callback(
    FuriHalInfraredTxSignalSentISRCallback callback, void* context);
void furi_hal_infrared_async_tx_start(uint32_t freq, float duty_cycle);
void furi_hal_infrared_async_tx_stop(void);
void furi_hal_infrared_async_tx_wait_termination(void);

/* --- async RX --- */
void furi_hal_infrared_async_rx_start(void);
void furi_hal_infrared_async_rx_stop(void);
void furi_hal_infrared_async_rx_set_timeout(uint32_t timeout_us);
void furi_hal_infrared_async_rx_set_capture_isr_callback(
    FuriHalInfraredRxCaptureCallback callback, void* ctx);
void furi_hal_infrared_async_rx_set_timeout_isr_callback(
    FuriHalInfraredRxTimeoutCallback callback, void* ctx);

/* --- misc --- */
bool furi_hal_infrared_is_busy(void);
FuriHalInfraredTxPin furi_hal_infrared_detect_tx_output(void);
void furi_hal_infrared_set_tx_output(FuriHalInfraredTxPin tx_pin);

#ifdef __cplusplus
}
#endif

#endif
