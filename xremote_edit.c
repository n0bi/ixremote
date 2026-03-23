/*!
 *  @file flipper-xremote/xremote_edit.c
    @license This project is released under the GNU GPLv3 License
 *  @copyright (c) 2023 Sandro Kalatozishvili (s.kalatoz@gmail.com)
 *
 * @brief Visual layout editor for IR MOTE custom remote slots.
 */

#include "xremote_edit.h"

#include <stdlib.h>
#include <string.h>

typedef enum {
    XRemoteEditSlotOk = 0,
    XRemoteEditSlotUp,
    XRemoteEditSlotDown,
    XRemoteEditSlotLeft,
    XRemoteEditSlotRight,
    XRemoteEditSlotCount,
} XRemoteEditSlot;

typedef struct {
    XRemoteAppButtons* buttons;
    XRemoteEditSlot selected_slot;
    bool hold_layer;
} XRemoteEditContext;

static uint32_t xremote_edit_view_exit_callback(void* context) {
    UNUSED(context);
    return XRemoteViewIRSubmenu;
}

static const char* xremote_edit_pretty_name(const char* name) {
    if(strcmp(name, XREMOTE_COMMAND_POWER) == 0) return "Power";
    if(strcmp(name, XREMOTE_COMMAND_INPUT) == 0) return "Input";
    if(strcmp(name, XREMOTE_COMMAND_SETUP) == 0) return "Setup";
    if(strcmp(name, XREMOTE_COMMAND_MENU) == 0) return "Menu";
    if(strcmp(name, XREMOTE_COMMAND_LIST) == 0) return "List";
    if(strcmp(name, XREMOTE_COMMAND_BACK) == 0) return "Back";
    if(strcmp(name, XREMOTE_COMMAND_OK) == 0) return "OK";
    if(strcmp(name, XREMOTE_COMMAND_UP) == 0) return "Up";
    if(strcmp(name, XREMOTE_COMMAND_DOWN) == 0) return "Down";
    if(strcmp(name, XREMOTE_COMMAND_LEFT) == 0) return "Left";
    if(strcmp(name, XREMOTE_COMMAND_RIGHT) == 0) return "Right";
    if(strcmp(name, XREMOTE_COMMAND_MUTE) == 0) return "Mute";
    if(strcmp(name, XREMOTE_COMMAND_MODE) == 0) return "Mode";
    if(strcmp(name, XREMOTE_COMMAND_VOL_UP) == 0) return "Vol+";
    if(strcmp(name, XREMOTE_COMMAND_VOL_DOWN) == 0) return "Vol-";
    if(strcmp(name, XREMOTE_COMMAND_NEXT_CHAN) == 0) return "Ch+";
    if(strcmp(name, XREMOTE_COMMAND_PREV_CHAN) == 0) return "Ch-";
    if(strcmp(name, XREMOTE_COMMAND_PLAY_PAUSE) == 0) return "Play/P";
    if(strcmp(name, XREMOTE_COMMAND_PLAY) == 0) return "Play";
    if(strcmp(name, XREMOTE_COMMAND_PAUSE) == 0) return "Pause";
    if(strcmp(name, XREMOTE_COMMAND_STOP) == 0) return "Stop";
    if(strcmp(name, XREMOTE_COMMAND_FAST_FORWARD) == 0) return "FFwd";
    if(strcmp(name, XREMOTE_COMMAND_FAST_BACKWARD) == 0) return "Rew";
    if(strcmp(name, XREMOTE_COMMAND_JUMP_FORWARD) == 0) return "Next";
    if(strcmp(name, XREMOTE_COMMAND_JUMP_BACKWARD) == 0) return "Prev";
    if(strcmp(name, XREMOTE_COMMAND_INFO) == 0) return "Info";
    if(strcmp(name, XREMOTE_COMMAND_EJECT) == 0) return "Eject";
    return name;
}

static XRemoteEditSlot xremote_edit_slot_from_key(InputKey key) {
    if(key == InputKeyOk) return XRemoteEditSlotOk;
    if(key == InputKeyUp) return XRemoteEditSlotUp;
    if(key == InputKeyDown) return XRemoteEditSlotDown;
    if(key == InputKeyLeft) return XRemoteEditSlotLeft;
    return XRemoteEditSlotRight;
}

static FuriString* xremote_edit_slot_string(XRemoteEditContext* ctx, XRemoteEditSlot slot) {
    XRemoteAppButtons* buttons = ctx->buttons;
    if(ctx->hold_layer) {
        if(slot == XRemoteEditSlotOk) return buttons->custom_ok_hold;
        if(slot == XRemoteEditSlotUp) return buttons->custom_up_hold;
        if(slot == XRemoteEditSlotDown) return buttons->custom_down_hold;
        if(slot == XRemoteEditSlotLeft) return buttons->custom_left_hold;
        return buttons->custom_right_hold;
    } else {
        if(slot == XRemoteEditSlotOk) return buttons->custom_ok;
        if(slot == XRemoteEditSlotUp) return buttons->custom_up;
        if(slot == XRemoteEditSlotDown) return buttons->custom_down;
        if(slot == XRemoteEditSlotLeft) return buttons->custom_left;
        return buttons->custom_right;
    }
}

static void xremote_edit_buttons_store(XRemoteAppButtons* buttons) {
    FuriString* path = buttons->app_ctx->file_path;
    infrared_remote_store(buttons->remote);
    xremote_app_extension_store(buttons, path);
}

static void xremote_edit_cycle_slot(XRemoteEditContext* ctx, bool reverse) {
    FuriString* current = xremote_edit_slot_string(ctx, ctx->selected_slot);
    const char* current_name = furi_string_get_cstr(current);
    int index = xremote_button_get_index(current_name);

    if(index < 0) index = 0;
    if(reverse) {
        index = (index == 0) ? (XREMOTE_BUTTON_COUNT - 1) : (index - 1);
    } else {
        index = (index + 1) % XREMOTE_BUTTON_COUNT;
    }

    furi_string_set_str(current, xremote_button_get_name(index));
    xremote_edit_buttons_store(ctx->buttons);
}

static void xremote_edit_draw_slot(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t w,
    uint8_t h,
    bool selected,
    XRemoteIcon icon,
    const char* label) {
    elements_slightly_rounded_frame(canvas, x, y, w, h);
    if(selected) {
        elements_slightly_rounded_box(canvas, x + 2, y + 2, w - 4, h - 4);
        canvas_set_color(canvas, ColorWhite);
    }

    xremote_canvas_draw_icon(canvas, x + 8, y + (h / 2), icon);
    canvas_set_font(canvas, FontSecondary);
    elements_multiline_text_aligned(
        canvas, x + (w / 2) + 4, y + h - 3, AlignCenter, AlignBottom, label);
    canvas_set_color(canvas, ColorBlack);
}

static const char* xremote_edit_slot_label(XRemoteEditContext* ctx, XRemoteEditSlot slot) {
    return xremote_edit_pretty_name(furi_string_get_cstr(xremote_edit_slot_string(ctx, slot)));
}

static void xremote_edit_draw_vertical(Canvas* canvas, XRemoteEditContext* ctx) {
    xremote_edit_draw_slot(
        canvas, 18, 28, 28, 14, ctx->selected_slot == XRemoteEditSlotUp, XRemoteIconArrowUp,
        xremote_edit_slot_label(ctx, XRemoteEditSlotUp));
    xremote_edit_draw_slot(
        canvas, 18, 46, 28, 14, ctx->selected_slot == XRemoteEditSlotOk, XRemoteIconEnter,
        xremote_edit_slot_label(ctx, XRemoteEditSlotOk));
    xremote_edit_draw_slot(
        canvas, 18, 64, 28, 14, ctx->selected_slot == XRemoteEditSlotDown, XRemoteIconArrowDown,
        xremote_edit_slot_label(ctx, XRemoteEditSlotDown));
    xremote_edit_draw_slot(
        canvas, 2, 46, 14, 14, ctx->selected_slot == XRemoteEditSlotLeft, XRemoteIconArrowLeft,
        xremote_edit_slot_label(ctx, XRemoteEditSlotLeft));
    xremote_edit_draw_slot(
        canvas, 48, 46, 14, 14, ctx->selected_slot == XRemoteEditSlotRight, XRemoteIconArrowRight,
        xremote_edit_slot_label(ctx, XRemoteEditSlotRight));

    elements_slightly_rounded_frame(canvas, 4, 90, 56, 24);
    canvas_set_font(canvas, FontSecondary);
    elements_multiline_text_aligned(
        canvas, 32, 100, AlignCenter, AlignCenter, ctx->hold_layer ? "HOLD LAYER" : "PRESS LAYER");
    elements_multiline_text_aligned(canvas, 32, 111, AlignCenter, AlignBottom, "Back: switch");
    elements_multiline_text_aligned(canvas, 32, 123, AlignCenter, AlignBottom, "Hold Back: exit");
}

static void xremote_edit_draw_horizontal(Canvas* canvas, XRemoteEditContext* ctx) {
    xremote_edit_draw_slot(
        canvas, 46, 12, 36, 12, ctx->selected_slot == XRemoteEditSlotUp, XRemoteIconArrowUp,
        xremote_edit_slot_label(ctx, XRemoteEditSlotUp));
    xremote_edit_draw_slot(
        canvas, 2, 27, 36, 12, ctx->selected_slot == XRemoteEditSlotLeft, XRemoteIconArrowLeft,
        xremote_edit_slot_label(ctx, XRemoteEditSlotLeft));
    xremote_edit_draw_slot(
        canvas, 46, 27, 36, 12, ctx->selected_slot == XRemoteEditSlotOk, XRemoteIconEnter,
        xremote_edit_slot_label(ctx, XRemoteEditSlotOk));
    xremote_edit_draw_slot(
        canvas, 90, 27, 36, 12, ctx->selected_slot == XRemoteEditSlotRight, XRemoteIconArrowRight,
        xremote_edit_slot_label(ctx, XRemoteEditSlotRight));
    xremote_edit_draw_slot(
        canvas, 46, 42, 36, 12, ctx->selected_slot == XRemoteEditSlotDown, XRemoteIconArrowDown,
        xremote_edit_slot_label(ctx, XRemoteEditSlotDown));

    elements_slightly_rounded_frame(canvas, 2, 2, 44, 10);
    canvas_set_font(canvas, FontSecondary);
    elements_multiline_text_aligned(
        canvas, 24, 9, AlignCenter, AlignBottom, ctx->hold_layer ? "HOLD" : "PRESS");
    elements_multiline_text_aligned(canvas, 128, 62, AlignRight, AlignBottom, "Bk:layer Hold:exit");
}

static void xremote_edit_draw_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    XRemoteViewModel* model = context;
    XRemoteEditContext* ctx = model->context;
    XRemoteAppContext* app_ctx = ctx->buttons->app_ctx;
    ViewOrientation orientation = app_ctx->app_settings->orientation;

    xremote_canvas_draw_header(canvas, orientation, "Layout");
    canvas_set_font(canvas, FontSecondary);
    if(orientation == ViewOrientationVertical) {
        elements_multiline_text_aligned(canvas, 0, 12, AlignLeft, AlignTop, "Press a slot twice");
        elements_multiline_text_aligned(canvas, 0, 20, AlignLeft, AlignTop, "OK next / hold prev");
        xremote_edit_draw_vertical(canvas, ctx);
    } else {
        elements_multiline_text_aligned(canvas, 126, 0, AlignRight, AlignTop, "OK next / hold prev");
        xremote_edit_draw_horizontal(canvas, ctx);
    }
}

static void xremote_edit_context_clear_callback(void* context) {
    XRemoteEditContext* ctx = context;
    if(!ctx) return;
    free(ctx);
}

static void xremote_edit_process(XRemoteView* view, InputEvent* event) {
    with_view_model(
        xremote_view_get_view(view),
        XRemoteViewModel * model,
        {
            XRemoteEditContext* ctx = model->context;
            if(event->type == InputTypeShort && event->key == InputKeyBack) {
                ctx->hold_layer = !ctx->hold_layer;
            } else if(event->type == InputTypeLong && event->key == InputKeyBack) {
                /* handled by input callback returning false */
            } else if(
                event->key == InputKeyUp || event->key == InputKeyDown || event->key == InputKeyLeft ||
                event->key == InputKeyRight || event->key == InputKeyOk) {
                XRemoteEditSlot slot = xremote_edit_slot_from_key(event->key);
                if(event->type == InputTypeShort) {
                    if(ctx->selected_slot == slot) {
                        xremote_edit_cycle_slot(ctx, false);
                    } else {
                        ctx->selected_slot = slot;
                    }
                } else if(event->type == InputTypeLong) {
                    if(ctx->selected_slot != slot) ctx->selected_slot = slot;
                    xremote_edit_cycle_slot(ctx, true);
                }
            }
        },
        true);
}

static bool xremote_edit_input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    XRemoteView* view = context;

    if(event->key == InputKeyBack && event->type == InputTypeLong) return false;
    xremote_edit_process(view, event);
    return true;
}

void xremote_edit_view_alloc(XRemoteApp* app, uint32_t view_id, XRemoteAppButtons* buttons) {
    xremote_app_view_free(app);

    XRemoteEditContext* context = malloc(sizeof(XRemoteEditContext));
    context->buttons = buttons;
    context->selected_slot = XRemoteEditSlotOk;
    context->hold_layer = false;

    XRemoteView* remote_view = xremote_view_alloc(
        buttons->app_ctx, xremote_edit_input_callback, xremote_edit_draw_callback);
    xremote_view_set_context(remote_view, context, xremote_edit_context_clear_callback);
    view_set_previous_callback(xremote_view_get_view(remote_view), xremote_edit_view_exit_callback);

    with_view_model(
        xremote_view_get_view(remote_view),
        XRemoteViewModel * model,
        {
            model->context = context;
            model->up_pressed = false;
            model->down_pressed = false;
            model->left_pressed = false;
            model->right_pressed = false;
            model->back_pressed = false;
            model->ok_pressed = false;
            model->hold = false;
        },
        true);

    app->view_ctx = remote_view;
    app->view_id = view_id;
}
