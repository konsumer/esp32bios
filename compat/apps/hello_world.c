/*
 * hello_world.c -- a Flipper Zero application, written to Flipper's normal API.
 *
 * Nothing in this file knows about esp32bios or BiosTable. It is the standard
 * Flipper GUI app pattern: a ViewPort with draw + input callbacks, a message
 * queue fed by the input callback, and a blocking event loop that exits on Back.
 * It compiles unchanged against the compat/ headers.
 *
 * On a real Flipper this is a .fap; here the same source recompiles for Xtensa
 * and runs on esp32bios hardware.
 */
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>

static void hello_draw_callback(Canvas* canvas, void* ctx) {
    UNUSED(ctx);
    canvas_clear(canvas);
    canvas_draw_frame(canvas, 0, 0, canvas_width(canvas), canvas_height(canvas));
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(
        canvas, canvas_width(canvas) / 2, 24, AlignCenter, AlignCenter, "Hello World");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, canvas_width(canvas) / 2, 44, AlignCenter, AlignCenter, "Back to exit");
}

static void hello_input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

int32_t hello_world_app(void* p) {
    UNUSED(p);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, hello_draw_callback, NULL);
    view_port_input_callback_set(view_port, hello_input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(event_queue, &event, FuriWaitForever) == FuriStatusOk) {
            if(event.type == InputTypePress && event.key == InputKeyBack) {
                running = false;
            }
        }
    }

    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    return 0;
}
