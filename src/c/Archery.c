// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO
    levels
        keep it still (and upright if gyro)
        battery fully charged?
        compass

    leave holes behind?
    special animation for robin hoods
    random animation for
        hanger
        bouncer
        pass-through
    screen shake on hit?
    vibe on hit

    shake too hard -> target falls off
    collision between shots and falling arrows (difficult)

    user config options (silly)
        carbon/aluminium/wooden arrows
        different faces
*/

#include <pebble.h>

#define DEBUG 1
#include "Macros.h"

static Window *s_window;

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

/// Return a GPoint that is `distance` away from `origin` at `angle`.
/// If `origin` is 0, this is equivalent to converting `angle` to a vector of magnitude `distance`.
static GPoint point_from_angle(GPoint origin, int32_t angle, int32_t distance) {
    return (GPoint) {
        .x = (int16_t)((sin_lookup(angle) * distance) / TRIG_MAX_RATIO) + origin.x,
        .y = (int16_t)((-cos_lookup(angle) * distance) / TRIG_MAX_RATIO) + origin.y
    };
}

// Normalize an angle to the range [0, TRIG_MAX_ANGLE] (from trig.c)
static int32_t normalize_angle(int32_t angle) {
    int32_t normalized_angle = ABS(angle) % TRIG_MAX_ANGLE;
    if (angle < 0) {
        normalized_angle = TRIG_MAX_ANGLE - normalized_angle;
    }
    return normalized_angle;
}

/******************************************************************************
 Background graphics
******************************************************************************/

static Layer *s_target_layer;

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


/******************************************************************************
 Arrow graphics
******************************************************************************/

static Layer *s_arrow_layer;

// Context required to draw an arrow animation sequence
typedef struct ArrowContext {
    int16_t frame;  // which frame of animation is it on. 0 for nothing.
    int32_t angle;  // in trigangle units
    int32_t length;  // of shaft
    int32_t distance;  // from centre
    GColor8 color;  // of fletchings

    // for falling
    GPoint offset_pos;  // offset from original hit location
    int32_t offset_angle;  // offset from original shaft angle
    GPoint velocity;
} ArrowContext;

#define MAX_ARROWS (30)
#define ARROW_NUM_FRAMES (4)
#define GRAVITY (1)

static ArrowContext s_arrows[MAX_ARROWS];
static ArrowContext s_arrows_falling[MAX_ARROWS];
static size_t s_arrows_falling_index = 0;  // the next slot in which to place a falling arrow

// Return the tip location of the arrow relative to `layer`
static GPoint arrow_tip(const Layer* layer, const ArrowContext* arrow) {
    const GRect bounds = layer_get_bounds(layer);
    const GPoint center = grect_center_point(&bounds);
    GPoint loc = point_from_angle(center, arrow->angle, arrow->distance);
    return (GPoint){loc.x + arrow->offset_pos.x, loc.y + arrow->offset_pos.y};
}

// Draw the arrow. Return true if it was on-screen.
static bool draw_arrow(Layer *layer, GContext *ctx, ArrowContext* arrow, int16_t wobble_deg, bool point) {

    // 3 degrees is required to visibly wobble when the arrow is straight vertical/horizontal
    const int32_t angle_deg_wrap_90 = TRIGANGLE_TO_DEG(arrow->angle) % 90;
    const bool is_straight = (
        MIN(ABSDIFF(angle_deg_wrap_90, 0),
            ABSDIFF(angle_deg_wrap_90, 90)
        ) < ((360 / 60) / 2)
    );
    if (is_straight) {
        wobble_deg = wobble_deg * 3;
    }

    const int32_t shaft_angle = arrow->angle + arrow->offset_angle + DEG_TO_TRIGANGLE(wobble_deg);

    // shaft
    const GPoint tip = arrow_tip(layer, arrow);
    const GPoint tail = point_from_angle(tip, shaft_angle, arrow->length);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_context_set_stroke_color(ctx, GColorWindsorTan);
    graphics_draw_line(ctx, tip, tail);

    // point
    if (point) {
        graphics_context_set_stroke_color(ctx, GColorLightGray);

        const GPoint shaft_end = point_from_angle(tip, shaft_angle, -4);
        graphics_draw_line(ctx, tip, shaft_end);

        const GPoint point_end = point_from_angle(tip, shaft_angle, -6);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_line(ctx, shaft_end, point_end);
    }

    // fletch
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, arrow->color);
    const int32_t nock_len = 4;
    const int32_t fletch_len = 7;  // both the length of each line and the length along the shaft
    for (int32_t dist_from_end = nock_len; dist_from_end < (fletch_len + nock_len); dist_from_end += 2) {
        const GPoint base = point_from_angle(tip, shaft_angle, arrow->length - dist_from_end);
        for (int16_t dir = -1; dir <= 1; dir += 2) {
            const GPoint tip = point_from_angle(base, shaft_angle + DEG_TO_TRIGANGLE(dir * 45), fletch_len);
            graphics_draw_line(ctx, base, tip);
        }
    }

    const GRect bounds = layer_get_bounds(layer);

    // It would be more accurate to check for intersection between the shaft and lower edge
    // to avoid arrows disappearing early when crossing the corner
    // but due to the way the arrows fall, they don't often do so in a noticeable way.
    return grect_contains_point(&bounds, &tip) || grect_contains_point(&bounds, &tail);
}

// Draw the initial swooshy speed arrival line prior to hit
static void arrow_frame_1(Layer *layer, GContext *ctx, ArrowContext* arrow) {
    const GPoint tip = arrow_tip(layer, arrow);
    const int32_t offscreen = 200;
    const GPoint tail = point_from_angle(tip, arrow->angle, offscreen);

    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_line(ctx, tip, tail);
}

// Wobble anticlockwise
static void arrow_frame_2(Layer *layer, GContext *ctx, ArrowContext* arrow) {
    draw_arrow(layer, ctx, arrow, -1, false);
}

// Wobble clockwise
static void arrow_frame_3(Layer *layer, GContext *ctx, ArrowContext* arrow) {
    draw_arrow(layer, ctx, arrow, 1, false);
}

// Final resting state
static void arrow_frame_4(Layer *layer, GContext *ctx, ArrowContext* arrow) {
    draw_arrow(layer, ctx, arrow, 0, false);
}

static void arrow_nextframe(void* context) {
    ArrowContext* arrow = (ArrowContext*)context;
    arrow->frame ++;
    if (arrow->frame < ARROW_NUM_FRAMES) {
        app_timer_register((arrow->frame < 1) ? 300 : 50, &arrow_nextframe, arrow);
    }
    layer_mark_dirty(s_arrow_layer);
}

// Start a new arrow removal sequence
static void arrow_pull(ArrowContext* original_arrow) {
    // move the arrow from s_arrows to s_arrows_falling
    s_arrows_falling[s_arrows_falling_index] = *original_arrow;
    ArrowContext *arrow = &s_arrows_falling[s_arrows_falling_index];
    s_arrows_falling_index = (s_arrows_falling_index + 1) % MAX_ARROWS;
    original_arrow->frame = 0;

    LOG("PULL #%u", s_arrows_falling_index);
    // pull out in the direction of the arrow
    arrow->velocity = point_from_angle(GPointZero, arrow->angle, 10);
}

// Start a new arrow shoot sequence
static void arrow_shoot(ArrowContext* arrow, int32_t angle, int32_t length, int32_t distance, int16_t delay) {
    ASSERT(delay >= 0);

    arrow_pull(arrow);

    arrow->angle = angle;
    arrow->length = length;
    arrow->distance = distance;
    arrow->frame = 0 - delay;
    arrow->color = (GColor8){.argb=rand() % UINT8_MAX};  // TODO limit colours to nice bright ones or signify hour/min/sec

    arrow_nextframe(arrow);
}

static void animate_shots(Layer* layer, GContext* ctx) {
    for (size_t i = 0; i < MAX_ARROWS; i++) {  // TODO sort by distance
        ArrowContext* const arrow = &s_arrows[i];
        if (arrow->frame) {
            switch(arrow->frame) {
            case 1:
                arrow_frame_1(layer, ctx, arrow);
                break;
            case 2:
                arrow_frame_2(layer, ctx, arrow);
                break;
            case 3:
                arrow_frame_3(layer, ctx, arrow);
                break;
            case 4:
                arrow_frame_4(layer, ctx, arrow);
                break;
            default:
                ASSERT(false);
                break;
            }
        }
    }
}

// Callback for timer to schedule the next falling animation update
static void continue_falling(void* context) {
    layer_mark_dirty(s_arrow_layer);
}

static void animate_fall(Layer* layer, GContext* ctx) {
    bool still_falling = false;
    for (size_t i = 0; i < MAX_ARROWS; i++) {  // TODO sort by distance
        ArrowContext* const arrow = &s_arrows_falling[i];
        if (arrow->frame) {
            // location
            arrow->offset_pos.x += arrow->velocity.x;
            arrow->offset_pos.y += arrow->velocity.y;

            // velocity
            arrow->velocity.y += 2;  // gravity
            if (ABS(arrow->velocity.x) > 2) {  // air resistance
                arrow->velocity.x -= SIGN(arrow->velocity.x);
            }

            // rotation; turn to point downwards (i.e. angle towards 0) with vertical air resistance
            if (arrow->velocity.y > 0) {
                // Vertical air resistance scales with downward-velocity and horizontalness.
                const int32_t velocity_scalar = 8 * (arrow->velocity.y * arrow->velocity.y);
                // Sine gives us horizontalness and also the direction in which to turn.
                const int32_t turn = (velocity_scalar * sin_lookup(arrow->angle)) / TRIG_MAX_RATIO;
                // shift angle so that 0 is mid-range i.e. DEG_TO_TRIGANGLE(180) for easy comparison
                const int32_t shifted_angle = normalize_angle(
                    arrow->angle + arrow->offset_angle + DEG_TO_TRIGANGLE(180));
                if (ABSDIFF(shifted_angle, DEG_TO_TRIGANGLE(180)) > ABS(turn)) {
                    arrow->offset_angle -= turn;
                } else {
                    arrow->offset_angle = -arrow->angle;
                    arrow->velocity.x = 0;
                }
            }

            if (draw_arrow(layer, ctx, arrow, 0, true)) {
                still_falling = true;
            } else {
                arrow->frame = 0;
            }
        }
    }
    if (still_falling) {
        app_timer_register(33, &continue_falling, NULL);  // 30fps
    }
}

// Callback to render s_arrow_layer
static void arrow_canvas(Layer* layer, GContext* ctx) {
    animate_shots(layer, ctx);
    animate_fall(layer, ctx);
}


/******************************************************************************
 Handlers
******************************************************************************/

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    s_state.hour = tick_time->tm_hour % 12;
    s_state.min = tick_time->tm_min;
    s_state.sec = tick_time->tm_sec;

    const GRect bounds = layer_get_bounds(s_arrow_layer);
    const GPoint center = grect_center_point(&bounds);

    int16_t delay = 1;

    if (units_changed & SECOND_UNIT) {
        const int32_t angle = s_state.sec * (TRIG_MAX_ANGLE / SECONDS_PER_MINUTE);
        const int32_t length = 70;
        const int32_t distance = rand() % (center.x - (length / 2));
        arrow_shoot(&s_arrows[2], angle, length, distance, delay);
        delay++;
    }

    if (units_changed & MINUTE_UNIT) {
        const int32_t angle = s_state.min * (TRIG_MAX_ANGLE / MINUTES_PER_HOUR);
        const int32_t length = 70;
        const int32_t distance = rand() % (center.x - (length / 2));
        arrow_shoot(&s_arrows[1], angle, length, distance, delay);
        delay++;
    }

    if (units_changed & HOUR_UNIT) {  // TODO update every 15 minutes?
        const int32_t angle = s_state.hour * (TRIG_MAX_ANGLE / (HOURS_PER_DAY / 2));
        const int32_t length = 50;
        const int32_t distance = rand() % (center.x - (length / 2));
        arrow_shoot(&s_arrows[0], angle, length, distance, delay);
        delay++;
    }
}

static void reshoot_all_arrows(void) {
    const time_t now = time(NULL);
    tick_handler(localtime(&now), HOUR_UNIT | MINUTE_UNIT);
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
    TRACE("accel_tap_handler");
    reshoot_all_arrows();
}


/******************************************************************************
 Main
******************************************************************************/

static void main_window_load(Window *window) {
    TRACE("main_window_load");
    Layer * const window_layer = window_get_root_layer(window);

    s_target_layer = layer_create(layer_get_frame(window_layer));
    layer_set_update_proc(s_target_layer, draw_target);
    layer_add_child(window_layer, s_target_layer);

    s_arrow_layer = layer_create(layer_get_frame(window_layer));
    layer_set_update_proc(s_arrow_layer, arrow_canvas);
    layer_add_child(window_layer, s_arrow_layer);

    tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
    accel_tap_service_subscribe(accel_tap_handler);
    // compass_service_subscribe();
    // battery_state_service_subscribe();

    reshoot_all_arrows();
}

static void main_window_unload(Window *window) {
    TRACE("main_window_unload");

    layer_destroy(s_target_layer);
    layer_destroy(s_arrow_layer);

    tick_timer_service_unsubscribe();
    accel_tap_service_unsubscribe();
    // battery_state_service_unsubscribe();
    // compass_service_unsubscribe();
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
