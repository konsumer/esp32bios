/*
 * input/input.h -- Flipper input API (shim). Same enums/struct shape as Flipper
 * so app source compiles unchanged.
 */
#ifndef COMPAT_INPUT_H
#define COMPAT_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    InputKeyUp,
    InputKeyDown,
    InputKeyRight,
    InputKeyLeft,
    InputKeyOk,
    InputKeyBack,
    InputKeyMAX,
} InputKey;

typedef enum {
    InputTypePress,    /* key down */
    InputTypeRelease,  /* key up */
    InputTypeShort,    /* click */
    InputTypeLong,     /* hold */
    InputTypeRepeat,   /* auto-repeat while held */
    InputTypeMAX,
} InputType;

typedef struct {
    uint32_t sequence;
    InputKey key;
    InputType type;
} InputEvent;

#ifdef __cplusplus
}
#endif

#endif
