// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#include <pebble.h>

typedef enum AnimateScrollState {
    AnimateScrollState_Init = 0,
    AnimateScrollState_Hidden,
    AnimateScrollState_Shown,
} AnimateScrollState;
void animate_scroll(Layer *layer, bool appear, bool from_below, AnimateScrollState* state);

uint32_t timestamp_ms(void);
void graphics_color_circle(GContext* ctx, GPoint p, uint16_t radius, GColor color);
void graphics_color_rect(GContext *ctx, GRect rect, uint16_t corner_radius,
                         GCornerMask corner_mask, GColor color);
GPoint point_from_angle(GPoint origin, int32_t angle, int32_t distance);
float fast_sqrt(const float x);

// Single vibe for duration
#if DISABLE_VIBE
    #define VIBE(duration) (void)duration
#else // !DISABLE_VIBE
    #define VIBE(duration) MACRO_START \
        LOG("VIBE %u", (duration)); \
         const uint32_t VIBE_segments[] = {(duration)}; \
        VibePattern VIBE_pat = { \
            .durations = VIBE_segments, \
            .num_segments = ARRAY_LENGTH(VIBE_segments), \
        }; \
        vibes_enqueue_custom_pattern(VIBE_pat); \
    MACRO_END
#endif // !DISABLE_VIBE
