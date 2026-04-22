// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// Miscellaneous standalone generic functions

#include "misc.h"

#include "macros.h"


// Return milliseconds since the epoch
uint32_t timestamp_ms(void) {
    time_t seconds = 0;
    const uint32_t millis = (uint32_t)time_ms(&seconds, NULL);
    return ((uint32_t)seconds * MS_PER_S) + millis;
}

// Fill a circle with color
void graphics_color_circle(GContext* ctx, GPoint p, uint16_t radius, GColor color){
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, p, radius);
}

// Fill a rect with color
void graphics_color_rect(GContext *ctx, GRect rect, uint16_t corner_radius,
                         GCornerMask corner_mask, GColor color){
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_rect(ctx, rect, 0, GCornerNone);
}

// Return a GPoint that is `distance` away from `origin` at `angle`.
// If `origin` is 0, this is equivalent to converting `angle` to a cartesian vector of magnitude `distance`.
GPoint point_from_angle(GPoint origin, int32_t angle, int32_t distance) {
    return (GPoint) {
        .x = (int16_t)((sin_lookup(angle) * distance) / TRIG_MAX_RATIO) + origin.x,
        .y = (int16_t)((-cos_lookup(angle) * distance) / TRIG_MAX_RATIO) + origin.y
    };
}

// quake 3 sqrt
float fast_sqrt(const float x) {
    const float xhalf = 0.5f * x;
    union {
        float x;
        int i;
    } u;
    u.x = x;
    u.i = 0x5f3759df - (u.i >> 1);  // initial guess
    return x * u.x * (1.5f - xhalf * u.x * u.x);  // Newton step
}

// Animate `layer` to `appear` or disappear by scrolling pixels vertically `from_below` or above.
// `was_visible` a pointer to an AnimateScrollState for this layer; will be updated.
void animate_scroll(Layer *layer, bool appear, bool from_below, AnimateScrollState* state) {
    const int16_t hide_offset = (from_below ? 1 : -1) * layer_get_bounds(layer).size.h;
    GPoint hidden_point = GPoint(0, hide_offset);
    GPoint zero = GPoint(0, 0);
    GPoint *from = NULL;
    GPoint *to = NULL;
    if (appear) {
        from = &hidden_point;
        to = &zero;
    } else {
        // from wherever it currently is
        to = &hidden_point;
    }

    if (*state == AnimateScrollState_Init) {  // start in the correct location
        GRect bounds = layer_get_bounds(layer);
        bounds.origin = *to;
        layer_set_bounds(layer, bounds);
    } else if (appear != (*state == AnimateScrollState_Shown)) {
        Animation *animation = (Animation *)property_animation_create_bounds_origin(layer, from, to);
        // animation_set_curve(animation, AnimationCurveLinear);
        // animation_set_duration(animation, 100);
        animation_schedule(animation);
    }
    *state = appear ? AnimateScrollState_Shown : AnimateScrollState_Hidden;
}
