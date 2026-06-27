/*
 * gui/gui.h -- Flipper GUI API (shim).
 */
#ifndef COMPAT_GUI_H
#define COMPAT_GUI_H

#include "gui/view_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_GUI "gui"

typedef struct Gui Gui;

typedef enum {
    GuiLayerDesktop,
    GuiLayerWindow,
    GuiLayerStatusBarLeft,
    GuiLayerStatusBarRight,
    GuiLayerFullscreen,
    GuiLayerMAX,
} GuiLayer;

void gui_add_view_port(Gui* gui, ViewPort* view_port, GuiLayer layer);
void gui_remove_view_port(Gui* gui, ViewPort* view_port);

#ifdef __cplusplus
}
#endif

#endif
