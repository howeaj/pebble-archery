// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO
    conditions
        pure luck (1/10,000)
        align compass with each arrow
            (extra: when arrows aren't close together)
        keep it still and upright and point north
        align "up" with each arrow
            (extra: when arrows aren't close together)
        robin hood
    vibe on non-random perfect hit
    condition complete celebration effects
        arrow spam
        vibes song
    track completed conditions
    hint every X shakes in a row

    hour markers around the edge
    Shrink target on Gabbro for green border
    leave holes behind?
    special animation for robin hoods
    random animation for
        hanger
        bouncer
        pass-through
    screen shake on hit?

    shake too hard -> target falls off
    collision between shots and falling arrows (difficult)

    user config options (silly)
        carbon/aluminium/wooden arrows
        different faces
        darts
*/

#include <pebble.h>

#define DEBUG true
#define SECOND_HAND false
#include "Macros.h"

static Window *s_window;


// todo delete this if there's no use
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
/// If `origin` is 0, this is equivalent to converting `angle` to a cartesian vector of magnitude `distance`.
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
 Achievements
******************************************************************************/

typedef uint32_t Achievements; // Bitfield, each bit indexed by Achievement

// Index of each achievement within Achievements.
// The meaning of each index should never change, as this is stored persistently.
typedef enum Achievement {
  ACHIEVEMENT_PURE_LUCK = 0,
  ACHIEVEMENT_COMPASS   = 1,

  ACHIEVEMENT_MAX,  // end-of-enum indicator; the number of achievements
} Achievement;

Achievements s_achievements;  // The user's obtained achievements. Stored persistently.

#define PERSIST_VERSION (0)  // The current persistent storage version

// persistent storage keys
#define PERSIST_KEY_VERSION (0)  // key for the version of the remaining storage layout
#define PERSIST_KEY_ACHIEVEMENTS (1)  // key for s_achievements


static bool is_persist_written_and_current_version(void) {
    return persist_read_int(PERSIST_KEY_VERSION) == PERSIST_VERSION;
}

/// Return true if achievements were loaded
static bool achievements_load(void) {
    StatusCode status = E_DOES_NOT_EXIST;
    if (is_persist_written_and_current_version()){
        status = persist_read_data(PERSIST_KEY_ACHIEVEMENTS, &s_achievements, sizeof(s_achievements));
        ASSERT(status == sizeof(s_achievements));
    }
    return status == sizeof(s_achievements);
}

static void achievements_save(void) {
    StatusCode status = persist_write_data(PERSIST_KEY_ACHIEVEMENTS, &s_achievements, sizeof(s_achievements));
    ASSERT(status == sizeof(s_achievements));

    if (status == sizeof(s_achievements)) {
        status = persist_write_int(PERSIST_KEY_VERSION, PERSIST_VERSION);
        ASSERT(status == sizeof(int32_t));
    }
}

// TODO reset option
// static void achievements_delete(void){
//     StatusCode status = persist_delete(PERSIST_KEY_ACHIEVEMENTS);
//     ASSERT((status == S_TRUE) || (status == E_DOES_NOT_EXIST));

//     status = persist_delete(PERSIST_KEY_VERSION);
//     ASSERT((status == S_TRUE) || (status == E_DOES_NOT_EXIST));
// }

// Add a single `achievement` to the given `achievements`
static inline void achievement_add(Achievements* achievements, Achievement achievement) {
    *achievements |= (((uint32_t)1u) << achievement);
}

// Return true if `achievement` is in `achievements`
static inline bool achievement_in(const Achievements* achievements, Achievement achievement) {
    return (*achievements & (((uint32_t)1u) << achievement)) != 0;
}

// TODO Return the number of complete achievements
// int16_t num_achievements(void) {
//     int16_t count = 0;
//     for (size_t i; i < sizeof(s_achievements) * 8; i++){
//         if ((1 << i) & s_achievements) {
//             ++ count;
//         }
//     }
//     return count;
// }

// Record the completion of an achievement
static void achievement_complete(Achievement achievement) {
    LOG("ACHIEVEMENT COMPLETE: %u", achievement);
    achievement_add(&s_achievements, (((uint32_t)1u) << achievement));
    achievements_save();
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
    // update tracking
    int16_t frame;  // which frame of animation is it on. 0 for nothing.
    AppTimer* timer;  // timer for next update

    // static attributes of the original shot
    int32_t angle;  // in trigangle units
    int32_t length;  // of shaft
    int32_t distance;  // from centre
    GColor8 color;  // of fletchings

    // for falling
    GPoint offset_pos;  // offset from original hit location
    int32_t offset_angle;  // offset from original shaft angle
    GPoint velocity;

    // for tracking achievement success
    bool is_manual_shot;
    Achievements achievements;  // achievements for which this arrow has met the conditions
} ArrowContext;

#define MAX_ARROWS (30)
#define ARROW_NUM_FRAMES (4)
#define GColorWood GColorWindsorTan

static ArrowContext s_arrows[MAX_ARROWS];
static ArrowContext s_arrows_falling[MAX_ARROWS];
static size_t s_arrows_falling_index = 0;  // the next slot in which to place a falling arrow


static void check_achievement_completion(void) {
    const size_t num_relevant_arrows = 2;  // Only the hour and second hand, which are always in the first 2 slots.
    int16_t success_count[ACHIEVEMENT_MAX] = {0};

    // count per-arrow achievement conditions
    for (size_t i = 0; i < num_relevant_arrows; i++) {
        ArrowContext* const arrow = &s_arrows[i];
        if (arrow->frame) {
            for (Achievement ach = (Achievement)0; ach < ACHIEVEMENT_MAX; ach++) {
                if (arrow->is_manual_shot || (ach == ACHIEVEMENT_PURE_LUCK)) {
                    if (achievement_in(&arrow->achievements, ach)) {
                        success_count[ach] += 1;
                    }
                }
            }
        }
    }

    // conditions must be attained on all relevant arrows to complete the achievement
    for (Achievement ach = (Achievement)0; ach < ACHIEVEMENT_MAX; ach++) {
        if (success_count[ach] == num_relevant_arrows) {
            achievement_complete(ach);
        } else if (success_count[ach] == (num_relevant_arrows - 1)) {
            if (s_arrows[0].frame && s_arrows[1].frame) {  // TODO implement properly
                LOG("SO CLOSE %u", ach);
            }
        }
    }

}

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
    graphics_context_set_stroke_color(ctx, GColorWood);
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
    const int32_t offscreen = 200;  // TODO calculate from root layer bounds
    const GPoint tail = point_from_angle(tip, arrow->angle, offscreen);

    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, GColorWood);
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

// Increment the arrow's shooting animation frame, schedule it for rendering,
// and set a timer for the next one (until completion).
static void arrow_nextframe(void* context) {
    ArrowContext* arrow = (ArrowContext*)context;
    arrow->frame ++;
    if (arrow->frame < ARROW_NUM_FRAMES) {
        const uint32_t delay_between_arrows = arrow->is_manual_shot ? 800 : 300;  // extra time for achiev criteria
        arrow->timer = app_timer_register((arrow->frame < 1) ? delay_between_arrows : 50, &arrow_nextframe, arrow);
        ASSERT(arrow->timer != NULL);
    } else {  // shot animation complete
        arrow->timer = NULL;
        check_achievement_completion();
    }
    layer_mark_dirty(s_arrow_layer);
}

// Start a new arrow removal sequence
static void arrow_pull(ArrowContext* original_arrow) {
    // Cancel any in-progress update timer.
    // Note this also skips achievement checking.
    // TODO is it possible for this event to be triggered in the same step as the timer
    //      in which case the timer's handler would still run?
    if (original_arrow->timer != NULL) {
        app_timer_cancel(original_arrow->timer);
        original_arrow->timer = NULL;
    }

    // move the arrow from s_arrows to s_arrows_falling
    s_arrows_falling[s_arrows_falling_index] = *original_arrow;
    ArrowContext *arrow = &s_arrows_falling[s_arrows_falling_index];
    s_arrows_falling_index = (s_arrows_falling_index + 1) % MAX_ARROWS;
    original_arrow->frame = 0;

    // pull out in the direction of the arrow
    arrow->velocity = point_from_angle(GPointZero, arrow->angle, 10);
}

// Start a new arrow shoot sequence
static void arrow_shoot(ArrowContext* arrow, int32_t angle, int32_t length, int16_t delay, bool is_manual_shot) {
    ASSERT(delay >= 0);

    arrow_pull(arrow);

    memset(arrow, 0, sizeof(*arrow));
    arrow->angle = angle;
    arrow->length = length;
    arrow->frame = 0 - delay;
    arrow->color = (GColor8){.argb=rand() % UINT8_MAX};  // TODO limit colours to nice bright ones or signify hour/min/sec
    arrow->is_manual_shot = is_manual_shot;

    arrow_nextframe(arrow);
}

// TODO accelerometer upright, +- 4000 which is +-4G. compass app uses y < -700 to switch to upright.

// Set the distance from centre that `arrow` will hit.
static void arrow_determine_accuracy(ArrowContext *arrow) {
    const int32_t target_radius = (MIN(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT) / 2); // TODO not necessarily; use frame
    const int32_t scoreband_radius = target_radius / 10;  // note each target colour is two scorebands
    const int32_t arrow_width = 3;

    // TODO always keep hour arrow fully on-screen, or at least one arrow
    int32_t max_distance = target_radius - (scoreband_radius);

    // Normally, you get a fixed small chance to hit the centre.
    // Chosen to get both arrows in centre on average weekly when shooting once per minute.
    bool hit_centre = (rand() % 100) == 0;
    if (hit_centre) {
        LOG("PERFECT HIT: RANDOM");
        achievement_add(&arrow->achievements, ACHIEVEMENT_PURE_LUCK);
    }

    // But if you do a manual reshoot, there are conditions to force a perfect hit.
    // If you get close to a condition, they give a clue by reducing the max distance.
    bool is_manual_reshoot = true;  // TODO
    int32_t clue_distance = max_distance;
    if (is_manual_reshoot) {
        CompassHeadingData compass;
        (void)compass_service_peek(&compass);

        // Condition #1: Align the arrow with compass North
        if (DEBUG || (compass.compass_status >= CompassStatusCalibrating)) {
            const int32_t clockwise_heading = TRIG_MAX_ANGLE - compass.true_heading;
            const int32_t compass_deviation = ABSDIFF_WRAP(arrow->angle, clockwise_heading, DEG_TO_TRIGANGLE(360));
            const int32_t hit_threshold = TRIG_MAX_ANGLE / MINUTES_PER_HOUR; // within 1 minute either side
            const int32_t clue_threshold = DEG_TO_TRIGANGLE(45);  // this is either side, so *2 total degrees
            LOG("clockwise_heading=%d, arrow->angle=%d, compass_deviation=%u",
                TRIGANGLE_TO_DEG(clockwise_heading),
                TRIGANGLE_TO_DEG(arrow->angle),
                TRIGANGLE_TO_DEG(compass_deviation)
            );
            if (compass_deviation < hit_threshold) {
                hit_centre = true;
                LOG("PERFECT HIT: COMPASS");
                achievement_add(&arrow->achievements, ACHIEVEMENT_COMPASS);
            } else if (compass_deviation < clue_threshold) {
                clue_distance = MIN(clue_distance, (max_distance * compass_deviation) / clue_threshold);
                LOG("CLUE: COMPASS");
            }
        } else {
            LOG("Compass error %i", compass.compass_status);
        }
    }

    // now we have evaluated the conditions, choose how close to the centre this arrow goes
    int32_t min_distance;
    if (hit_centre) {
        min_distance = 0;
        max_distance = scoreband_radius - arrow_width;
    } else {
        min_distance = scoreband_radius + arrow_width;
        max_distance = clue_distance;
    }
    arrow->distance = min_distance + (rand() % (max_distance - min_distance));
    LOG("%d ~ %d = %d", min_distance, max_distance, arrow->distance);
}

static void animate_shots(Layer* layer, GContext* ctx) {
    for (size_t i = 0; i < MAX_ARROWS; i++) {  // TODO sort by distance
        ArrowContext* const arrow = &s_arrows[i];
        if (arrow->frame > 0) {
            switch(arrow->frame) {
            case 1:
                arrow_determine_accuracy(arrow);
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
                LOG("WTF %d", arrow->frame);
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
    bool any_still_falling = false;
    for (size_t i = 0; i < MAX_ARROWS; i++) {  // TODO sort by distance
        ArrowContext* const arrow = &s_arrows_falling[i];
        if (arrow->frame) {
            // location
            arrow->offset_pos.x += arrow->velocity.x;
            arrow->offset_pos.y += arrow->velocity.y;

            // velocity
            arrow->velocity.y += 2;  // gravity TODO change according to pebble's orientation
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
                // TODO use ABSDIFF_WRAP?
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
                any_still_falling = true;
            } else {
                arrow->frame = 0;
            }
        }
    }
    if (any_still_falling) {
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

#define MANUAL_SHOT_TIMEUNITS (INT8_MAX & ~SECOND_UNIT)

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    TRACE("tick_handler %d", units_changed);
    s_state.hour = tick_time->tm_hour % 12;
    s_state.min = tick_time->tm_min;
    s_state.sec = tick_time->tm_sec;

    const bool is_manual_shot = (units_changed == MANUAL_SHOT_TIMEUNITS);
    int16_t delay = 1;

#if SECOND_HAND
    if (units_changed & SECOND_UNIT) {
        const int32_t angle = s_state.sec * (TRIG_MAX_ANGLE / SECONDS_PER_MINUTE);
        const int32_t length = 70;
        arrow_shoot(&s_arrows[2], angle, length, delay, is_manual_shot);
        delay++;
    }
#endif // SECOND_HAND

    if (units_changed & MINUTE_UNIT) {
        const int32_t angle = s_state.min * (TRIG_MAX_ANGLE / MINUTES_PER_HOUR);
        const int32_t length = 70;
        arrow_shoot(&s_arrows[1], angle, length, delay, is_manual_shot);
        delay++;

        { // hours
            const int32_t angle = (
                ((s_state.hour * MINUTES_PER_HOUR) + s_state.min)
                * (TRIG_MAX_ANGLE / (MINUTES_PER_DAY / 2))
            );
            const int32_t length = 50;
            arrow_shoot(&s_arrows[0], angle, length, delay, is_manual_shot);
            delay++;
        }
    }
}

static void reshoot_all_arrows(bool is_manual_shot) {
    const time_t now = time(NULL);
    tick_handler(localtime(&now), is_manual_shot ? MANUAL_SHOT_TIMEUNITS : (MINUTE_UNIT | HOUR_UNIT));
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
    TRACE("accel_tap_handler");
    reshoot_all_arrows(true);
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

    tick_timer_service_subscribe(SECOND_HAND ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
    accel_tap_service_subscribe(accel_tap_handler);

    achievements_load();

    reshoot_all_arrows(false);
}

static void main_window_unload(Window *window) {
    TRACE("main_window_unload");

    layer_destroy(s_target_layer);
    layer_destroy(s_arrow_layer);

    tick_timer_service_unsubscribe();
    accel_tap_service_unsubscribe();
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
