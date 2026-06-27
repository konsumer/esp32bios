/*
 * furi_compat.cpp -- the Flipper-API shim, implemented over the esp32bios
 * BiosTable. This is the piece that would live in the host firmware; a loaded
 * Flipper app resolves the furi / gui / canvas functions against it (by name,
 * via the ELF loader's symbol table) the way a FAP resolves against Flipper.
 *
 * The interesting part is bridging two execution models:
 *   Flipper: the app owns the main loop, blocking in furi_message_queue_get()
 *            while a separate GUI service thread renders and feeds input.
 *   esp32bios: a single cooperative thread.
 * We collapse that into one thread by running the render+input "pump" *inside*
 * furi_message_queue_get(): whenever the app blocks for an event, we draw the
 * active view port through the BIOS, poll the BIOS buttons, and synthesize
 * Flipper InputEvents. To the app it looks exactly like the real firmware.
 */
#include "furi.h"
#include "gui/gui.h"
#include "furi_hal_infrared.h"
#include "compat.h"
#include "bios.h"
#include "bios_caps.h"
#include <stdlib.h>
#include <string.h>

extern "C" {

/* ------------------------------------------------------------------ state --- */
static const BiosTable* g_bios = 0;

struct Canvas { uint16_t color; };           /* draws straight through g_bios */
static Canvas g_canvas;

struct ViewPort {
    ViewPortDrawCallback  draw;  void* draw_ctx;
    ViewPortInputCallback input; void* input_ctx;
    bool enabled;
};
struct Gui { ViewPort* active; };
static Gui g_gui;

struct FuriMessageQueue {
    uint8_t* buf; uint32_t cap, msz, count, head, tail;
};

/* Host calls this before launching the app. */
void compat_set_bios(const BiosTable* bios) { g_bios = bios; }

/* ------------------------------------------------------------- time/delay --- */
void furi_delay_ms(uint32_t ms) { if (g_bios) g_bios->delay_ms(ms); }
uint32_t furi_get_tick(void)    { return g_bios ? g_bios->millis() : 0; }

/* ----------------------------------------------------------------- canvas --- */
/* Flipper "ColorBlack" = ink on the LCD = a lit pixel for us. */
static uint16_t to_bios_color(uint16_t c) { return c == ColorWhite ? BIOS_BLACK : BIOS_WHITE; }

void canvas_clear(Canvas* c)                  { (void)c; if (g_bios) g_bios->display_clear(BIOS_BLACK); }
void canvas_commit(Canvas* c)                 { (void)c; }
size_t canvas_width(const Canvas* c)          { (void)c; return g_bios ? g_bios->display_width() : 0; }
size_t canvas_height(const Canvas* c)         { (void)c; return g_bios ? g_bios->display_height() : 0; }
size_t canvas_current_font_height(const Canvas* c) { (void)c; return 8; }
void canvas_set_color(Canvas* c, Color color) { c->color = (uint16_t)color; }
void canvas_set_font(Canvas* c, Font f)       { (void)c; (void)f; }

void canvas_draw_dot(Canvas* c, int32_t x, int32_t y) {
    if (g_bios) g_bios->display_pixel((int16_t)x, (int16_t)y, to_bios_color(c->color));
}
void canvas_draw_line(Canvas* c, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    /* Bresenham, drawing through the BIOS one pixel at a time. */
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1, err = dx + dy;
    for (;;) {
        canvas_draw_dot(c, x1, y1);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}
void canvas_draw_box(Canvas* c, int32_t x, int32_t y, size_t w, size_t h) {
    for (size_t j = 0; j < h; j++)
        for (size_t i = 0; i < w; i++) canvas_draw_dot(c, x + (int)i, y + (int)j);
}
void canvas_draw_frame(Canvas* c, int32_t x, int32_t y, size_t w, size_t h) {
    if (!w || !h) return;
    for (size_t i = 0; i < w; i++) { canvas_draw_dot(c, x+(int)i, y); canvas_draw_dot(c, x+(int)i, y+(int)h-1); }
    for (size_t j = 0; j < h; j++) { canvas_draw_dot(c, x, y+(int)j); canvas_draw_dot(c, x+(int)w-1, y+(int)j); }
}
void canvas_draw_str(Canvas* c, int32_t x, int32_t y, const char* str) {
    /* Flipper draws from the text baseline; the BIOS draws from the top-left.
     * Nudge up by ~a line so placement looks right. */
    if (g_bios) g_bios->display_text((int16_t)x, (int16_t)(y - 7), str, to_bios_color(c->color));
}
void canvas_draw_str_aligned(Canvas* c, int32_t x, int32_t y, Align h, Align v, const char* str) {
    int len = (int)strlen(str) * 6;
    if (h == AlignCenter) x -= len / 2; else if (h == AlignRight) x -= len;
    if (v == AlignCenter) y -= 4;       else if (v == AlignBottom) y -= 8;
    canvas_draw_str(c, x, y, str);
}
uint16_t canvas_string_width(Canvas* c, const char* str) { (void)c; return (uint16_t)(strlen(str) * 6); }

/* -------------------------------------------------------------- view port --- */
ViewPort* view_port_alloc(void) { return (ViewPort*)calloc(1, sizeof(ViewPort)); }
void view_port_free(ViewPort* v) { free(v); }
void view_port_enabled_set(ViewPort* v, bool e) { if (v) v->enabled = e; }
void view_port_draw_callback_set(ViewPort* v, ViewPortDrawCallback cb, void* ctx) { v->draw = cb; v->draw_ctx = ctx; }
void view_port_input_callback_set(ViewPort* v, ViewPortInputCallback cb, void* ctx) { v->input = cb; v->input_ctx = ctx; }
void view_port_update(ViewPort* v) { (void)v; /* we redraw every pump tick */ }

/* -------------------------------------------------------------------- gui --- */
void gui_add_view_port(Gui* gui, ViewPort* v, GuiLayer layer) { (void)layer; gui->active = v; if (v) v->enabled = true; }
void gui_remove_view_port(Gui* gui, ViewPort* v) { if (gui->active == v) gui->active = 0; }

/* ----------------------------------------------------------------- record --- */
void* furi_record_open(const char* id) {
    if (!strcmp(id, RECORD_GUI)) return &g_gui;
    return 0;
}
void furi_record_close(const char* id) { (void)id; }

/* --------------------------------------------------------- message queue --- */
FuriMessageQueue* furi_message_queue_alloc(uint32_t count, uint32_t size) {
    FuriMessageQueue* q = (FuriMessageQueue*)calloc(1, sizeof(FuriMessageQueue));
    q->buf = (uint8_t*)calloc(count, size);
    q->cap = count; q->msz = size;
    return q;
}
void furi_message_queue_free(FuriMessageQueue* q) { if (q) { free(q->buf); free(q); } }
uint32_t furi_message_queue_get_count(FuriMessageQueue* q) { return q ? q->count : 0; }

FuriStatus furi_message_queue_put(FuriMessageQueue* q, const void* msg, uint32_t timeout) {
    (void)timeout;
    if (q->count >= q->cap) return FuriStatusErrorResource;
    memcpy(q->buf + q->tail * q->msz, msg, q->msz);
    q->tail = (q->tail + 1) % q->cap; q->count++;
    return FuriStatusOk;
}

/* The pump: render the active view port and turn BIOS buttons into InputEvents.
 * Buttons A/B/C map to Ok/Back/Up. */
static void pump_once(void) {
    if (!g_bios) return;
    ViewPort* v = g_gui.active;
    if (v && v->enabled && v->draw) {
        g_canvas.color = ColorBlack;          /* default ink */
        v->draw(&g_canvas, v->draw_ctx);
        g_bios->display_flush();
    }
    static bool prev[InputKeyMAX] = {0};
    static const struct { uint8_t btn; InputKey key; } map[] = {
        { BIOS_BTN_A, InputKeyOk }, { BIOS_BTN_B, InputKeyBack }, { BIOS_BTN_C, InputKeyUp },
    };
    for (unsigned i = 0; i < sizeof(map)/sizeof(map[0]); i++) {
        bool now = g_bios->button_pressed(map[i].btn);
        if (now != prev[map[i].key]) {
            prev[map[i].key] = now;
            if (v && v->input) {
                InputEvent ev; ev.sequence = 0; ev.key = map[i].key;
                ev.type = now ? InputTypePress : InputTypeRelease;
                v->input(&ev, v->input_ctx);   /* app enqueues into its own queue */
            }
        }
    }
    g_bios->delay_ms(50);
}

FuriStatus furi_message_queue_get(FuriMessageQueue* q, void* msg, uint32_t timeout) {
    /* Block (running the pump) until an event lands in this queue. */
    while (q->count == 0) {
        pump_once();
        if (timeout != FuriWaitForever && q->count == 0) return FuriStatusErrorTimeout;
    }
    memcpy(msg, q->buf + q->head * q->msz, q->msz);
    q->head = (q->head + 1) % q->cap; q->count--;
    return FuriStatusOk;
}

/* ============================= furi_hal_infrared =========================== */
/* Flipper's IR HAL, implemented over the BIOS_CAP_INFRARED capability. Flipper's
 * model is async/ISR: the app installs a "get data" callback that the TX engine
 * polls for (duration, level) pairs. We drive that synchronously -- pull the
 * whole burst into a buffer, then hand it to the BIOS IR tx -- which is exactly
 * what the hardware does, just without the interrupt. */
static FuriHalInfraredTxGetDataISRCallback   s_tx_get;       static void* s_tx_get_ctx;
static FuriHalInfraredTxSignalSentISRCallback s_tx_sent;      static void* s_tx_sent_ctx;
static FuriHalInfraredRxCaptureCallback      s_rx_capture;    static void* s_rx_capture_ctx;
static uint32_t s_rx_timeout_us = 0;

static const BiosCapInfrared* ir_cap(void) {
    return g_bios ? (const BiosCapInfrared*)g_bios->capability(BIOS_CAP_INFRARED) : 0;
}

void furi_hal_infrared_async_tx_set_data_isr_callback(
    FuriHalInfraredTxGetDataISRCallback cb, void* ctx) { s_tx_get = cb; s_tx_get_ctx = ctx; }
void furi_hal_infrared_async_tx_set_signal_sent_isr_callback(
    FuriHalInfraredTxSignalSentISRCallback cb, void* ctx) { s_tx_sent = cb; s_tx_sent_ctx = ctx; }

void furi_hal_infrared_async_tx_start(uint32_t freq, float duty_cycle) {
    const BiosCapInfrared* ir = ir_cap();
    if (!ir || !ir->tx || !s_tx_get) return;          /* no IR hardware -> no-op */

    static uint32_t durations[1024];
    size_t n = 0;
    for (;;) {
        uint32_t duration = 0; bool level = false;
        FuriHalInfraredTxGetDataState st = s_tx_get(s_tx_get_ctx, &duration, &level);
        if (st == FuriHalInfraredTxGetDataStateLastDone) break;
        if (n < (sizeof(durations)/sizeof(durations[0]))) durations[n++] = duration;
        if (st == FuriHalInfraredTxGetDataStateDone) break;
    }
    ir->tx(durations, n, freq, duty_cycle);
    if (s_tx_sent) s_tx_sent(s_tx_sent_ctx);
}
void furi_hal_infrared_async_tx_stop(void) {}
void furi_hal_infrared_async_tx_wait_termination(void) {}   /* tx is synchronous */
bool furi_hal_infrared_is_busy(void) { return false; }

static void rx_trampoline(void* ctx, bool level, uint32_t duration_us) {
    (void)ctx;
    if (s_rx_capture) s_rx_capture(s_rx_capture_ctx, level, duration_us);
}
void furi_hal_infrared_async_rx_start(void) {
    const BiosCapInfrared* ir = ir_cap();
    if (ir && ir->rx_start) ir->rx_start(rx_trampoline, 0, s_rx_timeout_us);
}
void furi_hal_infrared_async_rx_stop(void) {
    const BiosCapInfrared* ir = ir_cap();
    if (ir && ir->rx_stop) ir->rx_stop();
}
void furi_hal_infrared_async_rx_set_timeout(uint32_t timeout_us) { s_rx_timeout_us = timeout_us; }
void furi_hal_infrared_async_rx_set_capture_isr_callback(
    FuriHalInfraredRxCaptureCallback cb, void* ctx) { s_rx_capture = cb; s_rx_capture_ctx = ctx; }
void furi_hal_infrared_async_rx_set_timeout_isr_callback(
    FuriHalInfraredRxTimeoutCallback cb, void* ctx) { (void)cb; (void)ctx; }

FuriHalInfraredTxPin furi_hal_infrared_detect_tx_output(void) { return FuriHalInfraredTxPinInternal; }
void furi_hal_infrared_set_tx_output(FuriHalInfraredTxPin tx_pin) { (void)tx_pin; }

/* ------------------------------------------------------ the symbol table --- */
/* The host's exported API surface -- what a loaded Flipper app links against.
 * This is the hand-written analog of Flipper's generated api_symbols.csv. */
struct CompatSym { const char* name; void* addr; };
#define SYM(f) { #f, (void*)f }
static const CompatSym k_symbols[] = {
    SYM(furi_delay_ms), SYM(furi_get_tick),
    SYM(furi_record_open), SYM(furi_record_close),
    SYM(furi_message_queue_alloc), SYM(furi_message_queue_free),
    SYM(furi_message_queue_put), SYM(furi_message_queue_get),
    SYM(furi_message_queue_get_count),
    SYM(canvas_clear), SYM(canvas_commit), SYM(canvas_width), SYM(canvas_height),
    SYM(canvas_current_font_height), SYM(canvas_set_color), SYM(canvas_set_font),
    SYM(canvas_draw_dot), SYM(canvas_draw_line), SYM(canvas_draw_box),
    SYM(canvas_draw_frame), SYM(canvas_draw_str), SYM(canvas_draw_str_aligned),
    SYM(canvas_string_width),
    SYM(view_port_alloc), SYM(view_port_free), SYM(view_port_enabled_set),
    SYM(view_port_draw_callback_set), SYM(view_port_input_callback_set),
    SYM(view_port_update),
    SYM(gui_add_view_port), SYM(gui_remove_view_port),
    /* furi_hal_infrared */
    SYM(furi_hal_infrared_async_tx_set_data_isr_callback),
    SYM(furi_hal_infrared_async_tx_set_signal_sent_isr_callback),
    SYM(furi_hal_infrared_async_tx_start), SYM(furi_hal_infrared_async_tx_stop),
    SYM(furi_hal_infrared_async_tx_wait_termination), SYM(furi_hal_infrared_is_busy),
    SYM(furi_hal_infrared_async_rx_start), SYM(furi_hal_infrared_async_rx_stop),
    SYM(furi_hal_infrared_async_rx_set_timeout),
    SYM(furi_hal_infrared_async_rx_set_capture_isr_callback),
    SYM(furi_hal_infrared_async_rx_set_timeout_isr_callback),
    SYM(furi_hal_infrared_detect_tx_output), SYM(furi_hal_infrared_set_tx_output),
};
#undef SYM

uintptr_t compat_resolve(const char* name) {
    for (unsigned i = 0; i < sizeof(k_symbols)/sizeof(k_symbols[0]); i++)
        if (!strcmp(name, k_symbols[i].name)) return (uintptr_t)k_symbols[i].addr;
    return 0;
}

} /* extern "C" */
