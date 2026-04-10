// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO
    arrows
    arrows animation
    arrows random accuracy
    shake to clear & reshoot
    levels
        keep it still (and upright if gyro)
        battery fully charged?
        compass
*/

#include <pebble.h>

#define DEBUG 1
#include "Macros.h"

static Window *s_window;
static Layer *s_target_layer;
static Layer *s_arrow_layer;

typedef struct State {
    int hour;
    int min;
    int sec;
} State;
State s_state;


/******************************************************************************
 Generic functions
******************************************************************************/

/// Fill a circle with color
static inline void graphics_color_circle(GContext* ctx, GPoint p, uint16_t radius, GColor color){
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, p, radius);
}

/// Fill a rect with color
static inline void graphics_color_rect(GContext *ctx, GRect rect, uint16_t corner_radius,
                                       GCornerMask corner_mask, GColor color){
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_rect(ctx, rect, 0, GCornerNone);
}

/// Return a GPoint that is `radius` away from `origin` at `angle`.
static GPoint point_from_angle(GPoint origin, int32_t angle, int32_t radius) {
    return (GPoint) {
        .x = (int16_t)((sin_lookup(angle) * radius) / TRIG_MAX_RATIO) + origin.x,
        .y = (int16_t)((-cos_lookup(angle) * radius) / TRIG_MAX_RATIO) + origin.y
    };
}


/******************************************************************************
 Graphics
******************************************************************************/

static void draw_target(Layer *layer, GContext *ctx) {
    const GRect bounds = layer_get_bounds(layer);
    const GPoint center = grect_center_point(&bounds);
    const int16_t target_radius = MIN(bounds.size.h, bounds.size.w) / 2;

#if PBL_RECT
    // grass with drop-shadow
    graphics_color_rect(ctx, bounds, 0, GCornerNone, GColorMayGreen);
    graphics_color_circle(ctx, (GPoint){center.x - 5, center.y + 5}, target_radius + 5, GColorDarkGreen);
#endif // PBL_RECT

    // face
    const GColor colors[] = {
        GColorWhite,
        GColorBlack,
        GColorBlue,
        GColorRed,
        GColorYellow,
    };
    const int16_t ring_width = target_radius / ARRAY_LENGTH(colors);
    for (size_t i = 0; i < ARRAY_LENGTH(colors); i++) {
        graphics_color_circle(ctx, center, (ARRAY_LENGTH(colors) - i) * ring_width, colors[i]);
    }
    // 10spot
    graphics_color_circle(ctx, center, ring_width / 2, GColorPastelYellow);

    // black divider between all rings, except white between the blacks
    // ... looks bad
    // for (size_t i = 0; i < ARRAY_LENGTH(colors); i++) {
    //     graphics_context_set_stroke_color(ctx, (i == 6) ? GColorLightGray : GColorBlack);
    //     graphics_draw_circle(ctx, center, (i + 1) * ring_width);
    //     LOG("radius %d", (i + 1) * ring_width);
    // }
}

void draw_arrow(Layer *layer, GContext *ctx) {
    const GRect bounds = layer_get_bounds(layer);
    const GPoint center = grect_center_point(&bounds);

    int16_t angle = s_state.sec * (TRIG_MAX_ANGLE / SECONDS_PER_MINUTE);
    int32_t length = 50;
    int32_t distance = rand() % (center.x - (length / 2));

    const GPoint nose = point_from_angle(center, angle, distance);
    const GPoint tail = point_from_angle(center, angle, distance + length);

    graphics_context_set_stroke_width(ctx, 2);
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_line(ctx, nose, tail);
}


/******************************************************************************
 Handlers
******************************************************************************/

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    s_state.hour = tick_time->tm_hour % 12;
    s_state.min = tick_time->tm_min;
    s_state.sec = tick_time->tm_sec;
    layer_mark_dirty(s_arrow_layer);
}


/******************************************************************************
 Main
******************************************************************************/

static void main_window_load(Window *window) {
    TRACE("main_window_load");
    Layer *window_layer = window_get_root_layer(window);

    s_target_layer = layer_create(layer_get_frame(window_layer));
    layer_set_update_proc(s_target_layer, draw_target);
    layer_add_child(window_layer, s_target_layer);

    s_arrow_layer = layer_create(layer_get_frame(window_layer));
    layer_set_update_proc(s_arrow_layer, draw_arrow);
    layer_add_child(window_layer, s_arrow_layer);

    tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
    // battery_state_service_subscribe();
    // accel_tap_service_subscribe();
    // battery_state_service_subscribe();
}

static void main_window_unload(Window *window) {
    TRACE("main_window_unload");

    layer_destroy(s_target_layer);
    layer_destroy(s_arrow_layer);

    tick_timer_service_unsubscribe();
    // battery_state_service_unsubscribe();
    // compass_service_unsubscribe();
    // accel_tap_service_unsubscribe();
}

static void init(void) {
    LOG("Init");
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload,
    });
    window_stack_push(s_window, true);
}

static void deinit(void) {
    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
