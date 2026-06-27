/*
 * furi.h -- Flipper-API compatibility shim (core), implemented over the esp32bios
 * BiosTable. Just enough of furi/ for a standard GUI app to compile unchanged.
 *
 * This is NOT Flipper code -- it's a clean re-declaration of the same API surface
 * so Flipper app *source* recompiles against esp32bios. See compat/README.md.
 */
#ifndef COMPAT_FURI_H
#define COMPAT_FURI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNUSED(x) ((void)(x))
#define FuriWaitForever 0xFFFFFFFFU

/* In real firmware these abort; for the shim they're cheap guards. */
#define furi_assert(expr) ((void)(expr))
#define furi_check(expr)  ((void)(expr))

#define FURI_LOG_E(tag, ...) ((void)0)
#define FURI_LOG_W(tag, ...) ((void)0)
#define FURI_LOG_I(tag, ...) ((void)0)
#define FURI_LOG_D(tag, ...) ((void)0)

typedef enum {
    FuriStatusOk = 0,
    FuriStatusError = -1,
    FuriStatusErrorTimeout = -2,
    FuriStatusErrorResource = -3,
    FuriStatusErrorParameter = -4,
} FuriStatus;

void furi_delay_ms(uint32_t ms);
uint32_t furi_get_tick(void);

/* --- message queue --- */
typedef struct FuriMessageQueue FuriMessageQueue;
FuriMessageQueue* furi_message_queue_alloc(uint32_t msg_count, uint32_t msg_size);
void       furi_message_queue_free(FuriMessageQueue* q);
FuriStatus furi_message_queue_put(FuriMessageQueue* q, const void* msg, uint32_t timeout);
FuriStatus furi_message_queue_get(FuriMessageQueue* q, void* msg, uint32_t timeout);
uint32_t   furi_message_queue_get_count(FuriMessageQueue* q);

/* --- record / service registry --- */
void* furi_record_open(const char* record_id);
void  furi_record_close(const char* record_id);

#ifdef __cplusplus
}
#endif

#endif
