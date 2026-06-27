/*
 * gui/view_port.h -- Flipper ViewPort API (shim).
 */
#ifndef COMPAT_VIEW_PORT_H
#define COMPAT_VIEW_PORT_H

#include <stdbool.h>
#include "gui/canvas.h"
#include "input/input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ViewPort ViewPort;

typedef void (*ViewPortDrawCallback)(Canvas* canvas, void* context);
typedef void (*ViewPortInputCallback)(InputEvent* event, void* context);

ViewPort* view_port_alloc(void);
void view_port_free(ViewPort* view_port);
void view_port_enabled_set(ViewPort* view_port, bool enabled);
void view_port_draw_callback_set(
    ViewPort* view_port, ViewPortDrawCallback callback, void* context);
void view_port_input_callback_set(
    ViewPort* view_port, ViewPortInputCallback callback, void* context);
void view_port_update(ViewPort* view_port);

#ifdef __cplusplus
}
#endif

#endif
