// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO
    fix trophy location on time 2
    save last arrow conditions to pull at next init

    low battery indicator: slow wobbly arrow

    conditions
        align "up" with each arrow
        robin hood

    leave holes behind?
    special animation for robin hoods
    random animation for
        hanger
        bouncer
        pass-through
    target shake on hit?

    shake too hard -> target falls off
    extra achievement when arrows are far apart?
    collision between shots and falling arrows (difficult)

    digital display as scoreboard

    user config options
        reduce arrow randomness
        vibe options
        turn off accel/compass features

        carbon/aluminium/wooden arrows
        different faces
        darts
*/

#include <pebble.h>

// debug options
#define DEMO false
#define DEBUG false
#define SECOND_HAND false
#define DISABLE_VIBE false
#define FORCE_COMPASS (DEMO || false)  // fake compass calibrated
#define FORCE_LUCK false  // always hit centre

#include "Macros.h"

static Window *s_window;
static bool s_initialising = true;

// todo delete this if there's no use
typedef struct State {
    int hour;
    int min;
    int sec;
} State;
State s_state;

// The target actually gets drawn as several rings of equal width, so we must divide
// and then multiply TARGET_RADIUS by TARGET_NUM_RINGS to account for that rounding error in advance.
#define TARGET_NUM_RINGS (5)

#if PBL_PLATFORM_GABBRO
    #define GRASS_WIDTH (MIN(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT) / 14)  // just enough to fit trophies
    #define TARGET_RADIUS ((((MIN(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT) / 2) - GRASS_WIDTH) \
                            / TARGET_NUM_RINGS) * TARGET_NUM_RINGS)
#else // !PBL_PLATFORM_GABBRO
    #define TARGET_RADIUS (((MIN(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT) / 2) \
                            / TARGET_NUM_RINGS) * TARGET_NUM_RINGS)
#endif // !PBL_PLATFORM_GABBRO

// Note each target ring colour is two scorebands.
// This may have rounding errors.
#define SCOREBAND_WIDTH (TARGET_RADIUS / 10)

// Fonts
#if PBL_DISPLAY_WIDTH >= 240
    #define FONT_KEY_MED FONT_KEY_GOTHIC_24
    #define FONT_H_MED (24)
    #define FONT_KEY_SMALL FONT_KEY_GOTHIC_18
    #define FONT_H_SMALL (18)
#elif PBL_DISPLAY_WIDTH >= 160
    #if PBL_ROUND
        #define FONT_KEY_MED FONT_KEY_GOTHIC_14
        #define FONT_H_MED (14)
        #define FONT_KEY_SMALL FONT_KEY_GOTHIC_09
        #define FONT_H_SMALL (9)
    #else // PBL_RECT
        #define FONT_KEY_MED FONT_KEY_GOTHIC_18
        #define FONT_H_MED (18)
        #define FONT_KEY_SMALL FONT_KEY_GOTHIC_14
        #define FONT_H_SMALL (14)
    #endif // PBL_RECT
#else // PBL_DISPLAY_WIDTH < 160
    #define FONT_KEY_MED FONT_KEY_GOTHIC_14
    #define FONT_H_MED (14)
    #define FONT_KEY_SMALL FONT_KEY_GOTHIC_09
    #define FONT_H_SMALL (9)
#endif // PBL_DISPLAY_WIDTH < 160


/******************************************************************************
 Generic functions
******************************************************************************/

// Single vibe for duration
#if DISABLE_VIBE
    #define VIBE(duration) (void)duration
#else // !DISABLE_VIBE
    #define VIBE(duration) MACRO_START \
        LOG("VIBE %u", (duration)); \
        static const uint32_t VIBE_segments[] = {(duration)}; \
        VibePattern VIBE_pat = { \
            .durations = VIBE_segments, \
            .num_segments = ARRAY_LENGTH(VIBE_segments), \
        }; \
        vibes_enqueue_custom_pattern(VIBE_pat); \
    MACRO_END
#endif // !DISABLE_VIBE

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

// from measurements https://discord.com/channels/221364737269694464/264746316477759489/1492708645425909902
#define ACCEL_NOISE_MAX (50)  // the max change in each accel reading when perfectly still
#define ACCEL_1G (1000)  // 1G of force in accel sensor units

// return true on success
static bool accel_service_peek_logged(AccelData* data) {
    const int retval = accel_service_peek(data);
    if (retval == -1) {
        LOG("ACCEL ERROR: not running");
    } else if (retval == -2) {
        LOG("ACCEL ERROR: subscribed");
    } else if (retval < 0) {
        LOG("ACCEL ERROR: unknown");
    }
    return retval >= 0;
}

// quake 3 sqrt
static float fast_sqrt(const float x) {
    const float xhalf = 0.5f * x;
    union {
        float x;
        int i;
    } u;
    u.x = x;
    u.i = 0x5f3759df - (u.i >> 1);  // initial guess
    return x * u.x * (1.5f - xhalf * u.x * u.x);  // Newton step
}

/// Animate `layer` to `appear` or disappear by scrolling pixels vertically `from_below` or above.
/// `was_visible` a pointer to a static bool; will be updated
static void animate_scroll(Layer *layer, bool appear, bool from_below, bool* was_visible) {
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

    if (s_initialising) {  // start in the correct location
        GRect bounds = layer_get_bounds(layer);
        bounds.origin = *to;
        layer_set_bounds(layer, bounds);
    } else if (appear != *was_visible) {
        Animation *animation = (Animation *)property_animation_create_bounds_origin(layer, from, to);
        // animation_set_curve(animation, AnimationCurveLinear);
        // animation_set_duration(animation, 100);
        animation_schedule(animation);
    }
    *was_visible = appear;
}


/******************************************************************************
 Compass
******************************************************************************/

#define COMPASS_CALIB_POLL_RATE_MS (2000)  // how often to check progress while calibrating. Must be < PEEK_TIMEOUT_MS.

static TextLayer* s_layer_status_text;

static void show_status_message(bool show){
    static bool was_visible = false;
    animate_scroll((Layer*)s_layer_status_text, show, true, &was_visible);
}

static void status_text_create(Layer * parent) {
    GRect parent_bounds = layer_get_bounds(parent);
    const int16_t background_h = (FONT_H_SMALL * 3) / 2;
    const int16_t origin_y = PBL_IF_RECT_ELSE(
        parent_bounds.size.h - background_h,  // bottom of rect
        (parent_bounds.size.h * 3) / 4        // bit higher for circle
    );
    s_layer_status_text = text_layer_create(
        GRect(0, origin_y, parent_bounds.size.w, background_h)
    );
    text_layer_set_text(s_layer_status_text, "");
    text_layer_set_text_alignment(s_layer_status_text, GTextAlignmentCenter);
    text_layer_set_font(s_layer_status_text, fonts_get_system_font(FONT_KEY_SMALL));
    text_layer_set_background_color(s_layer_status_text, PBL_IF_COLOR_ELSE(GColorMayGreen, GColorWhite));
    text_layer_set_text_color(s_layer_status_text, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
    show_status_message(false);
}

// return true on success
static bool compass_service_peek_logged(CompassHeadingData* data) {
    bool success = true;

    CompassHeadingData localData = {0};
    if (data == NULL) {
        data = &localData;
    }

    (void)compass_service_peek(data);
    if (!FORCE_COMPASS && (data->compass_status != CompassStatusCalibrated)) {
        LOG("COMPASS ERROR: %i", data->compass_status);
        success = false;
    }
    return success;
}

#if PBL_COMPASS
static void compass_calibrate_callback(void* context) {
    // TODO detect lack of movement and abort calibration to save power
    CompassHeadingData data = {0};
    if (compass_service_peek_logged(&data)) {
        LOG("Compass calibration complete");
        show_status_message(false);
    } else {
        if (data.compass_status == CompassStatusCalibrating) {
            text_layer_set_text(s_layer_status_text, "keep moving! compass calibrating");
        }
        app_timer_register(COMPASS_CALIB_POLL_RATE_MS, compass_calibrate_callback, NULL);
    }
}

static void compass_start_calibration(void) {
    LOG("Starting compass calibration");
    text_layer_set_text(s_layer_status_text, "move wrist to calibrate compass");
    show_status_message(true);
    app_timer_register(COMPASS_CALIB_POLL_RATE_MS, compass_calibrate_callback, NULL);
}
#endif  // !PBL_COMPASS


/******************************************************************************
 Achievements
******************************************************************************/

typedef uint32_t Achievements; // Bitfield, each bit indexed by Achievement

// Index of each achievement within Achievements.
// The meaning of each index should never change, as this is stored persistently.
typedef enum Achievement {
  ACHIEVEMENT_PURE_LUCK = 0,
  ACHIEVEMENT_COMPASS   = 1,
  ACHIEVEMENT_UPRIGHT   = 2,

  ACHIEVEMENT_MAX,  // end-of-enum indicator; the number of achievements
} Achievement;

Achievements s_achievements = 0;  // The user's obtained achievements. Stored persistently.

#define PERSIST_VERSION (1)  // The current persistent storage version

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
static inline bool achievement_in(Achievements achievements, Achievement achievement) {
    return (achievements & (((uint32_t)1u) << achievement)) != 0;
}

// Return the number of complete achievements
static int16_t num_achievements(void) {
    int16_t count = 0;
    for (size_t i = 0; i < sizeof(s_achievements) * 8; i++){
        if ((1 << i) & s_achievements) {
            ++ count;
        }
    }
    return count;
}


/******************************************************************************
 Background graphics
******************************************************************************/

static Layer *s_layer_target;

static void draw_target(Layer *layer, GContext *ctx) {
    const GRect bounds = layer_get_bounds(layer);
    const GPoint center = grect_center_point(&bounds);

#if !PBL_CHALK
    // grass with drop-shadow
    graphics_color_rect(ctx, bounds, 0, GCornerNone, GColorMayGreen);
    const uint16_t shadow_radius = PBL_IF_RECT_ELSE(TARGET_RADIUS + 5, TARGET_RADIUS);  // bigger looks better on rect
    graphics_color_circle(ctx, (GPoint){center.x - 5, center.y + 5}, shadow_radius, GColorDarkGreen);
#endif // !PBL_CHALK

    // face
    const GColor colors[TARGET_NUM_RINGS] = {
#if PBL_COLOR
        GColorWhite,
        GColorBlack,
        GColorBlue,
        GColorRed,
        GColorYellow,
#else // PBL_BW
        GColorWhite,
        GColorLightGray,
        GColorWhite,
        GColorLightGray,
        GColorWhite,
#endif // PBL_BW
    };
    const int16_t ring_width = TARGET_RADIUS / TARGET_NUM_RINGS;
    for (size_t i = 0; i < TARGET_NUM_RINGS; i++) {
        graphics_color_circle(ctx, center, (TARGET_NUM_RINGS - i) * ring_width, colors[i]);
    }
    // 10spot
    graphics_color_circle(ctx, center, ring_width / 2, PBL_IF_COLOR_ELSE(GColorPastelYellow, GColorLightGray));

    // clock indices
    GRect target_bounds = grect_crop(bounds, (bounds.size.w / 2) - TARGET_RADIUS);
#if !PBL_PLATFORM_CHALK
    // correct for rounding errors, I guess?
    target_bounds.size.w += 2;
    target_bounds.size.h += 2;
#endif // PBL_PLATFORM_CHALK
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack));
    #define NUM_CLOCK_LINES (12)
    for (int32_t i = 0; i < NUM_CLOCK_LINES; i++) {
        const int16_t angle = i * DEG_TO_TRIGANGLE(360 / NUM_CLOCK_LINES);
        const GPoint circumference_point = gpoint_from_polar(target_bounds, GOvalScaleModeFitCircle, angle);
        int32_t line_len = ((i % 3) == 0) ? 10 : 5;
        const GPoint inner_point = point_from_angle(circumference_point, angle, -line_len);
        graphics_draw_line(ctx, circumference_point, inner_point);
    }
}

static Layer *s_layer_trophy;
static GBitmap* s_icon_trophy;

static void draw_trophies(Layer *layer, GContext *ctx){
    const int16_t num_trophies = num_achievements();
    if (num_trophies == 0){
        return;
    };

#if PBL_PLATFORM_GABBRO
    const GRect bounds = grect_crop(layer_get_bounds(layer), GRASS_WIDTH);
#elif PBL_PLATFORM_CHALK
    const GRect bounds = grect_crop(layer_get_bounds(layer), SCOREBAND_WIDTH * 2);  // trophies inside white ring
#elif PBL_PLATFORM_EMERY  // TODO maybe do this at the top level instead
    const GRect bounds = grect_crop(layer_get_bounds(layer), 5);
#else // PBL_RECT && !PBL_PLATFORM_EMERY
    const GRect bounds = grect_crop(layer_get_bounds(layer), 1);
#endif // PBL_RECT && !PBL_PLATFORM_EMERY

    graphics_context_set_compositing_mode(ctx, GCompOpSet); // enable transparency
    const GSize icon_size = gbitmap_get_bounds(s_icon_trophy).size;

#if PBL_RECT
    const int16_t margin = 1;  // between each trophy
    GRect icon_bounds = {
        .origin = {
            // to centre: .x = bounds.origin.x + (bounds.size.w / 2) - ((num_trophies * icon_size.w) / 2) - (margin * (num_trophies - 1)),
            .x = bounds.origin.x,
            .y = bounds.origin.y },
        .size = icon_size
    };
    for (int16_t i = 0; i < num_trophies; i++) {
        graphics_draw_bitmap_in_rect(ctx, s_icon_trophy, icon_bounds);
        icon_bounds.origin.x += icon_size.w + margin;
    }
#else // PBL_ROUND

#if PBL_PLATFORM_CHALK
    const int16_t angle_per_icon = DEG_TO_TRIGANGLE(15);
#else // !PBL_PLATFORM_CHALK
    const int16_t angle_per_icon = DEG_TO_TRIGANGLE(10);
#endif // !PBL_PLATFORM_CHALK

    int16_t angle = DEG_TO_TRIGANGLE(0) - (((num_trophies - 1) * angle_per_icon) / 2);
    for (int16_t i = 0; i < num_trophies; i++) {
        const GPoint circumference_point = gpoint_from_polar(bounds, GOvalScaleModeFitCircle, angle);
        const GRect icon_bounds = {
            .origin = {
                .x = circumference_point.x - (icon_size.w / 2),  // TODO + sine width/2 to support more trophies
                .y = circumference_point.y - (icon_size.h + 1)},
            .size = icon_size
        };
        graphics_draw_bitmap_in_rect(ctx, s_icon_trophy, icon_bounds);
        angle += angle_per_icon;
    }
#endif // PBL_ROUND
}


/******************************************************************************
 Achievement notification
******************************************************************************/

static TextLayer *s_layer_trophy_notify;

// init s_layer_trophy_notify
static void trophy_notify_create(Layer * parent) {
    GRect parent_bounds = layer_get_bounds(parent);
    s_layer_trophy_notify = text_layer_create(GRect(0, 30, parent_bounds.size.w, (FONT_H_MED * 5) / 2));
    text_layer_set_text(s_layer_trophy_notify, "");
    text_layer_set_text_alignment(s_layer_trophy_notify, GTextAlignmentCenter);
    text_layer_set_font(s_layer_trophy_notify, fonts_get_system_font(FONT_KEY_MED));
    text_layer_set_background_color(s_layer_trophy_notify, GColorImperialPurple);
    text_layer_set_text_color(s_layer_trophy_notify, GColorWhite);
}

static bool s_notify_showing = false;

// returns true if a notification was dismissed
static bool achievement_notify(Achievement achievement) {
    bool dismissed = false;

    switch (achievement) {
    case ACHIEVEMENT_PURE_LUCK:
        text_layer_set_text(s_layer_trophy_notify, "Trophy won: LUCKY DAY\ntime to buy a lottery ticket!");
        break;
    case ACHIEVEMENT_COMPASS:
        text_layer_set_text(s_layer_trophy_notify, "Trophy won: NORTH STAR\nwho magnetized my arrows?");
        break;
    case ACHIEVEMENT_UPRIGHT:
        text_layer_set_text(s_layer_trophy_notify, "Trophy won: PERFECT SETUP\njust like a real target");
        break;
    case ACHIEVEMENT_MAX:
        if (s_notify_showing) {
            dismissed = true;
        }
        break;
    default:
        ASSERT(false);
        break;
    }
    animate_scroll((Layer*)s_layer_trophy_notify, achievement != ACHIEVEMENT_MAX, false, &s_notify_showing);

    return dismissed;
}

static inline bool achievement_dismiss(void) {
    return achievement_notify(ACHIEVEMENT_MAX);
}

static void vibe_celebration(void) {
    static const uint32_t segments[] = {
        // final fantasy victory theme; 100BPM, 1 quarter beat = 150ms
        75, 75,
        75, 75,
        75, 75,
        75*5, 75,  // 3
        75*5, 75,  // 3
        75*5, 75,  // 3
        75, 75*3,  // 2
        75, 75,
        75*6       // 3
    };
    VibePattern pat = {
        .durations = segments,
        .num_segments = ARRAY_LENGTH(segments),
    };
    vibes_enqueue_custom_pattern(pat);
}

static void arrow_spam_start(void);  // TODO move

static void achievement_complete(Achievement achievement) {
    LOG("ACHIEVEMENT COMPLETE: %u", achievement);
    if (!achievement_in(s_achievements, achievement)) {
        LOG("NEW ACHIEVEMENT %d", achievement);

        achievement_notify(achievement);
        achievement_add(&s_achievements, achievement);
        achievements_save();
        vibe_celebration();
    }
    arrow_spam_start();
}


/******************************************************************************
 Arrow graphics
******************************************************************************/

static Layer *s_layer_arrow;

typedef enum ShotReason {
    SHOT_REASON_TICK = 0,
    SHOT_REASON_INIT,
    SHOT_REASON_SHAKE,
    SHOT_REASON_COMPASS,
    SHOT_REASON_SPAM
} ShotReason;

static inline bool shot_reason_is_manual(ShotReason shot_reason) {
    return (shot_reason == SHOT_REASON_SHAKE) || (shot_reason == SHOT_REASON_INIT);
}


// Context required to draw an arrow animation sequence
typedef struct ArrowContext {
    // update tracking
    int16_t frame;  // which frame of animation is it on. 0 for nothing.
    AppTimer* timer;  // timer for next update
    bool hit_effects_started;

    // static attributes of the original shot
    ShotReason shot_reason;
    int32_t angle;  // in trigangle units
    int32_t length;  // of shaft
    int32_t distance;  // from centre
    GColor8 color;  // of fletchings

    // for falling
    GPoint offset_pos;  // offset from original hit location
    int32_t offset_angle;  // offset from original shaft angle
    GPoint velocity;

    // for tracking achievement success
    Achievements achievements;  // achievements for which this arrow has met the conditions
} ArrowContext;

#define MAX_ARROWS (15)
#define ARROW_NUM_FRAMES (4)  // number of frames in the arrow shoot animation
#define GColorWood GColorWindsorTan
#define ARROW_DISTANCE_UNINITIALISED (-1)
#define ARROW_LENGTH_LONG ((TARGET_RADIUS * 7) / 10)
#define ARROW_LENGTH_SHORT ((TARGET_RADIUS) / 2)

static ArrowContext s_arrows[MAX_ARROWS];
// 3 indices of s_arrows are reserved for the hour/minutes/seconds hand
#define HOUR_ARROW_INDEX    (0)
#define MINUTE_ARROW_INDEX  (1)
#define SECOND_ARROW_INDEX  (2)
#define COMPASS_ARROW_INDEX (3)
#define FIRST_SPAM_ARROW_INDEX (2)  // the remaining arrows < MAX_ARROWS are used for arrow spam

#define LAST_ARROW_SHOT HOUR_ARROW_INDEX  // the last arrow to be shot on each regular round


static ArrowContext s_arrows_falling[MAX_ARROWS];
// s_arrows_falling is just a circular buffer, no reserved slots
static size_t s_arrows_falling_index = 0;  // the next slot in which to place a falling arrow


static inline bool is_hour_hand(const ArrowContext* arrow) {
    return arrow - s_arrows == 0;
}

static inline bool arrow_spam_is_shooting(void);  // TODO move

// triggering_arrow is the arrow whose shot completion triggered this check
static void check_achievement_completion(ArrowContext* triggering_arrow) {
    const size_t num_relevant_arrows = 2;  // Only the hour and second hand, which are always in the first 2 slots.
    int16_t success_count[ACHIEVEMENT_MAX] = {0};

    // count per-arrow achievement conditions
    if (triggering_arrow->shot_reason != SHOT_REASON_COMPASS) {
        for (size_t i = 0; i < num_relevant_arrows; i++) {
            ArrowContext* const arrow = &s_arrows[i];
            if (arrow->frame) {
                for (Achievement ach = (Achievement)0; ach < ACHIEVEMENT_MAX; ach++) {
                    if (shot_reason_is_manual(arrow->shot_reason) || (ach == ACHIEVEMENT_PURE_LUCK)) {
                        if (achievement_in(arrow->achievements, ach)) {
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
}

// Return the tip location of the arrow relative to `layer`
static GPoint arrow_tip(const Layer* layer, const ArrowContext* arrow) {
    const GRect bounds = layer_get_bounds(layer);
    const GPoint center = grect_center_point(&bounds);
    GPoint loc = point_from_angle(center, arrow->angle, arrow->distance);
    return (GPoint){loc.x + arrow->offset_pos.x, loc.y + arrow->offset_pos.y};
}

// Return true if the given arrow should attempt to be drawn by the next arrow_canvas()
static inline bool arrow_should_draw(const ArrowContext* arrow) {
    return arrow->frame > 0;
}

// Draw the arrow. Return true if it was on-screen.
static bool draw_arrow(Layer *layer, GContext *ctx, ArrowContext* arrow, int16_t wobble_deg, bool point) {

    // 3 degrees is required to visibly wobble when the arrow is straight vertical/horizontal
    if (wobble_deg != 0) {
        const int32_t angle_deg_wrap_90 = TRIGANGLE_TO_DEG(arrow->angle) % 90;
        const bool is_straight = (
            MIN(ABSDIFF(angle_deg_wrap_90, 0),
                ABSDIFF(angle_deg_wrap_90, 90)
            ) < ((360 / 60) / 2)
        );
        if (is_straight) {
            wobble_deg = wobble_deg * 3;
        }
    }

    const int32_t shaft_angle = arrow->angle + arrow->offset_angle + DEG_TO_TRIGANGLE(wobble_deg);

    // shaft
    const GPoint tip = arrow_tip(layer, arrow);
    const GPoint tail = point_from_angle(tip, shaft_angle, arrow->length);
    const GColor shaft_color = (arrow->shot_reason == SHOT_REASON_COMPASS) ? GColorDarkCandyAppleRed : GColorWood;
    graphics_context_set_stroke_width(ctx, 2);
    graphics_context_set_stroke_color(ctx, shaft_color);
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

// start effects that should occur on arrow hit
static void arrow_hit_effects(ArrowContext* arrow) {
    if (!arrow->hit_effects_started) {
        arrow->hit_effects_started = true;
        // vibe
        if ((arrow->shot_reason != SHOT_REASON_TICK) && (arrow->shot_reason != SHOT_REASON_SPAM)) {
            if ((DEMO && (arrow->distance < 10)) || arrow->achievements != 0) {
                VIBE(300);
            } else {
                if (DEMO || (arrow->shot_reason == SHOT_REASON_INIT)) {
                    VIBE(100);
                }
            }
        }
    }
}

// Initial hit and wobble anticlockwise
static void arrow_frame_2(Layer *layer, GContext *ctx, ArrowContext* arrow) {
    draw_arrow(layer, ctx, arrow, -1, false);

#if !DEBUG
    arrow_hit_effects(arrow);
#endif // !DEBUG
}

// Wobble clockwise
static void arrow_frame_3(Layer *layer, GContext *ctx, ArrowContext* arrow) {
    draw_arrow(layer, ctx, arrow, 1, false);

#if DEBUG // emulator vibe is inaccurate, here looks better
    arrow_hit_effects(arrow);
#endif // DEBUG
}

static void arrow_pull(ArrowContext* original_arrow); // TODO move

// Final resting state
static void arrow_frame_4(Layer *layer, GContext *ctx, ArrowContext* arrow) {
    draw_arrow(layer, ctx, arrow, 0, false);

    if (arrow->shot_reason == SHOT_REASON_COMPASS) {
        arrow_pull(arrow);
    }
}

// Increment the arrow's shooting animation frame, schedule it for rendering,
// and set a timer for the next one (until completion).
static void arrow_nextframe(void* context) {
    ArrowContext* arrow = (ArrowContext*)context;
    arrow->frame ++;
    if (arrow->frame < ARROW_NUM_FRAMES) {
        // extra delay on shake to complete achievement conditions
        const uint32_t delay_between_arrows = (arrow->shot_reason == SHOT_REASON_SHAKE) ? 800 : 300;
        arrow->timer = app_timer_register(arrow_should_draw(arrow) ? 50 : delay_between_arrows,
                                          &arrow_nextframe, arrow);
        ASSERT(arrow->timer != NULL);
    } else {  // shot animation complete
        arrow->timer = NULL;
        if (arrow->shot_reason != SHOT_REASON_SPAM) {
            check_achievement_completion(arrow);
        }
    }
    layer_mark_dirty(s_layer_arrow);
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

    if (arrow_should_draw(original_arrow)){
        // move the arrow from s_arrows to s_arrows_falling
        s_arrows_falling[s_arrows_falling_index] = *original_arrow;
        ArrowContext *arrow = &s_arrows_falling[s_arrows_falling_index];
        s_arrows_falling_index = (s_arrows_falling_index + 1) % MAX_ARROWS;
        original_arrow->frame = 0;  // mark as don't draw

        // pull out in the direction of the arrow
        // note this velocity should be at least the length of the arrowpoint
        arrow->velocity = point_from_angle(GPointZero, arrow->angle, (PBL_DISPLAY_WIDTH < 200) ? 6 : 10);

        layer_mark_dirty(s_layer_arrow);
    }
}

// Start a new arrow shoot sequence
static void arrow_shoot(ArrowContext* arrow, int32_t angle, int32_t length, int16_t delay, ShotReason shot_reason) {
    ASSERT(delay >= 0);
    LOG("Shooting arrow %d", arrow - s_arrows);

    arrow_pull(arrow);

    memset(arrow, 0, sizeof(*arrow));
    arrow->shot_reason = shot_reason;
    arrow->distance = ARROW_DISTANCE_UNINITIALISED;
    arrow->angle = angle;
    arrow->length = length;
    arrow->frame = 0 - delay;

    if (shot_reason == SHOT_REASON_COMPASS) {
        arrow->color = GColorDarkCandyAppleRed;
    } else {
        arrow->color = (GColor8){.argb=rand() % UINT8_MAX};  // TODO limit colours to nice bright ones or signify hour/min/sec
    }
    arrow_nextframe(arrow);
}

#if DEMO
static void demo_override_arrow_hits(ArrowContext* arrow) {
    static int counter = 2;
    if (counter > 0) {
        if (is_hour_hand(arrow)) {
            arrow->color = GColorCyan;
        } else {
            arrow->color = GColorMagenta;
        }
    } else if (counter > -2) {
        if (is_hour_hand(arrow)) {
            arrow->color = GColorRed;
        } else{
            arrow->color = GColorGreen;
        }
    } else {
        if (is_hour_hand(arrow)) {
            arrow->distance = 5;
            arrow->color = GColorGreen;
        } else{
            arrow->distance = 13;// gabbro 55;
            arrow->color = GColorYellow;
        }
        // achievement_add(&arrow->achievements, ACHIEVEMENT_COMPASS);
    }
    counter--;
}
#endif // DEMO


// Arrowspam celebration;
// Quickly shoot random arrows until the countdown ends or arrow_spam_stop() is called.
#define SPAM_COUNDOWN_START (50)  // i.e. num_arrows_to_spam - 2
static uint16_t s_spam_countdown = 0;  // 0 for off. 1 for done (so we know to pull them). >1 in progress.
static bool s_spam_index = FIRST_SPAM_ARROW_INDEX;  // the next spam arrow to shoot
static AppTimer* s_spam_timer = NULL;
typedef enum SpamStyle {
    SPAM_STYLE_RANDOM = 0,
    SPAM_STYLE_CENTRE,
    SPAM_STYLE_SPIRAL,
    SPAM_STYLE_MAX  // end-of-enum indicator
} SpamStyle;
static SpamStyle s_spam_style = SPAM_STYLE_MAX;
static inline bool arrow_spam_is_shooting(void) {
    return s_spam_countdown > 1;
}
static void arrow_spam_callback(void* context) {
    if (s_spam_countdown > 1) {
        const int16_t arrow_index = (
            FIRST_SPAM_ARROW_INDEX + (
                (SPAM_COUNDOWN_START - s_spam_countdown)
                % (MAX_ARROWS - FIRST_SPAM_ARROW_INDEX)
            )
        );
        ArrowContext* arrow = &s_arrows[arrow_index];
        arrow_shoot(
            arrow,
            rand() % TRIG_MAX_ANGLE,
            (rand() % ARROW_LENGTH_LONG) + (ARROW_LENGTH_SHORT/2),
            0,
            SHOT_REASON_SPAM
        );
        switch (s_spam_style) {
        case SPAM_STYLE_RANDOM:
#if !DEMO
            break;
#endif // !DEMO
        case SPAM_STYLE_CENTRE:
            arrow->distance = 0;
            break;
        case SPAM_STYLE_SPIRAL:
            arrow->angle = DEG_TO_TRIGANGLE(s_spam_countdown * 7);
            arrow->distance = (TARGET_RADIUS * s_spam_countdown) / SPAM_COUNDOWN_START;
            break;
        default:
            ASSERT(false);
            break;
        }
        s_spam_countdown --;
        s_spam_timer = app_timer_register(300, arrow_spam_callback, NULL);
    }
}
// Return true if stopped an ongoing spam
static bool arrow_spam_stop(void) {
    if (s_spam_countdown >= 1) {
        LOG("arrow_spam_stopped");
        s_spam_countdown = 0;
        if (s_spam_timer != NULL) {
            app_timer_cancel(s_spam_timer);
            s_spam_timer = NULL;
        }
        for (size_t i = FIRST_SPAM_ARROW_INDEX; i < MAX_ARROWS; i++) {
            arrow_pull(&s_arrows[i]);
        }
        s_spam_style = (SpamStyle)(((uint8_t)s_spam_style + 1) % (uint8_t)SPAM_STYLE_MAX);  // cycle through styles
        return true;
    }
    return false;
}
static void arrow_spam_start(void) {
    TRACE("arrow_spam_start");
    ASSERT(s_spam_timer == NULL);
    if (s_spam_style == SPAM_STYLE_MAX) {  // start with random style
        s_spam_style = (SpamStyle)(rand() % (int)SPAM_STYLE_MAX);
    }
    s_spam_index = FIRST_SPAM_ARROW_INDEX;
    s_spam_countdown = SPAM_COUNDOWN_START;
    arrow_spam_callback(NULL);
}

// ACHIEVEMENT_UPRIGHT: Keep it still and upright (slight tilt back) like a real target
static int32_t evaluate_achievement_upright(ArrowContext *arrow, int32_t max_distance, int32_t* clue_distance,
                                            const AccelData *accel) {
    bool success = false;

    // note ACCEL_MAX_NOISE is 50, or about 5 degrees of tilt from pure gravity
    // Relative to the pebble's screen:
    //  x axis is sideways; through the buttons. +x is towards the select button.
    //  y axis is upwards; through the wristband. +y is up.
    //  z axis is perpendicular; through the screen. +z is towards the viewer.

    // calculate deviations i.e. how far (in accel counts) they were from meeting the criteria

    // x should be parallel with the ground; no forces
    const int16_t deviation_x = MAX(0, ABSDIFF(accel->x, 0) - ACCEL_NOISE_MAX);

    // the hypotenuse of y and z should sum to 1G; no additional forces
    const int16_t deviation_yz = MAX(0, ABSDIFF(fast_sqrt(SQUARE(accel->y) + SQUARE(accel->z)), ACCEL_1G));

    // accept between 5 and 15 degrees tilted backwards
    // i.e. -10 degrees += 5. small negative z, large negative y
    // The hypotenuse is gravity, which is always 1G
    //    /|<- angle away from perfectly straight up
    //   / |
    //  /  y
    // /_z_|
    // TODO just calculate all these once
    const int16_t ideal_angle = DEG_TO_TRIGANGLE(-10);
    const int16_t ideal_y = (cos_lookup(ideal_angle) * ACCEL_1G) / TRIG_MAX_RATIO;
    const int16_t ideal_z = (sin_lookup(ideal_angle) * ACCEL_1G) / TRIG_MAX_RATIO;
    const int16_t threshold_angle = 5;
    const int16_t threshold_y = (cos_lookup(threshold_angle) * ACCEL_1G) / TRIG_MAX_RATIO;
    const int16_t threshold_z = (sin_lookup(threshold_angle) * ACCEL_1G) / TRIG_MAX_RATIO;
    const int16_t deviation_y = MAX(0, ABSDIFF(accel->y, ideal_y) - threshold_y - ACCEL_NOISE_MAX);
    const int16_t deviation_z = MAX(0, ABSDIFF(accel->z, ideal_z) - threshold_z - ACCEL_NOISE_MAX);

    const int32_t total_deviation = deviation_x + deviation_y + deviation_z + deviation_yz;
    // LOG("x=%d, y=%d, z=%d, tot=%d, devX=%d, devY=%d, devZ=%d, devYZ=%d",
    //     accel->x, accel->y, accel->z, total_deviation, deviation_x, deviation_y, deviation_z, deviation_yz);
    const bool is_upright = (total_deviation == 0);

    static AccelData accel_other = {0};
    STATIC_ASSERT(LAST_ARROW_SHOT == HOUR_ARROW_INDEX);
    if (is_hour_hand(arrow)) {
        // must not move between the two shots
        const bool is_still = MAX(
            ABSDIFF(accel->x, accel_other.x),
            MAX(ABSDIFF(accel->y, accel_other.y),
                ABSDIFF(accel->z, accel_other.z))
        ) < ACCEL_NOISE_MAX;
        // LOG("is_still=%s", BOOL_TO_STR(is_still));

        success = is_upright && is_still;
    } else {
        success = is_upright;
        accel_other = *accel;
    }

    if (success) {
        LOG("PERFECT HIT: UPRIGHT");
        achievement_add(&arrow->achievements, ACHIEVEMENT_COMPASS);
    } else {
        LOG("CLUE: UPRIGHT");
        // increase arrow accuracy if we get close
        const int16_t clue_threshold = 500; // TODO test
        if (total_deviation < clue_threshold) {
            *clue_distance = (max_distance * total_deviation) / clue_threshold;
        }
    }
    return success;
}

// ACHIEVEMENT_COMPASS: Align the arrow with compass North
static bool evaluate_achievement_compass(ArrowContext *arrow, int32_t max_distance, int32_t* clue_distance,
                                            const CompassHeadingData *compass) {
    bool success = false;
    const int32_t clockwise_heading = TRIG_MAX_ANGLE - compass->true_heading;
    const int32_t compass_deviation = ABSDIFF_WRAP(arrow->angle, clockwise_heading, DEG_TO_TRIGANGLE(360));
    const int32_t hit_threshold = (5 * TRIG_MAX_ANGLE) / (2 * MINUTES_PER_HOUR); // within 2.5 minutes either side
    const int32_t clue_threshold = DEG_TO_TRIGANGLE(45);  // this is either side, so *2 total degrees
    LOG("clockwise_heading=%d, arrow->angle=%d, compass_deviation=%u",
        TRIGANGLE_TO_DEG(clockwise_heading),
        TRIGANGLE_TO_DEG(arrow->angle),
        TRIGANGLE_TO_DEG(compass_deviation)
    );
    if (compass_deviation < hit_threshold) {
        success = true;
        LOG("PERFECT HIT: COMPASS");
        achievement_add(&arrow->achievements, ACHIEVEMENT_COMPASS);
    } else if (compass_deviation < clue_threshold) {
        *clue_distance = MIN(*clue_distance, (max_distance * compass_deviation) / clue_threshold);
        LOG("CLUE: COMPASS");
    }
    return success;
}


// Set the distance from centre that `arrow` will hit.
static void arrow_determine_accuracy(ArrowContext *arrow) {
    ASSERT(arrow->distance == ARROW_DISTANCE_UNINITIALISED);

    int32_t max_distance = TARGET_RADIUS - SCOREBAND_WIDTH;
    if (is_hour_hand(arrow)) {  // ensure hour always on-screen
        // TODO allow further in the corners of rect screens
        max_distance = (MIN(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT) / 2) - arrow->length;
    }

    // Normally, you get a fixed small chance to hit the centre.
    // Chosen to get both arrows in centre on average weekly when shooting once per minute.
    bool hit_centre = (rand() % 100) == 0;
#if FORCE_LUCK
    if (arrow->shot_reason != SHOT_REASON_SPAM) {
        hit_centre = true;
    }
#endif // FORCE_LUCK
    if (hit_centre) {
        LOG("PERFECT HIT: RANDOM");
        achievement_add(&arrow->achievements, ACHIEVEMENT_PURE_LUCK);
    }

    // But if you do a manual reshoot, there are conditions to force a perfect hit.
    // If you get close to a condition, they give a clue by reducing the max distance.
    int32_t clue_distance = max_distance;
    if (shot_reason_is_manual(arrow->shot_reason)) {
        CompassHeadingData compass = {0};
        const bool compass_valid = compass_service_peek_logged(&compass);
        AccelData accel = {0};
        const bool accel_valid = accel_service_peek_logged(&accel);

        if (compass_valid) {
            hit_centre |= evaluate_achievement_compass(arrow, max_distance, &clue_distance, &compass);
        }
        if (accel_valid) {
            hit_centre |= evaluate_achievement_upright(arrow, max_distance, &clue_distance, &accel);
        }
    }

    // now we have evaluated the conditions, choose how close to the centre this arrow goes
    const int32_t arrow_width = 3;
    int32_t min_distance;
    if (hit_centre) {
        min_distance = 0;
        max_distance = SCOREBAND_WIDTH - arrow_width;
    } else {
        min_distance = SCOREBAND_WIDTH + arrow_width;
        max_distance = clue_distance;
    }

#if DEMO
    min_distance = 10;
    if (!is_hour_hand(arrow)){
        max_distance = (MIN(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT) / 2) - arrow->length;
    }
#endif // DEMO

    arrow->distance = min_distance + (rand() % (max_distance - min_distance));
    // LOG("%d ~ %d = %d", min_distance, max_distance, arrow->distance);

#if DEMO
    if ((arrow->shot_reason != SHOT_REASON_COMPASS) && (arrow->shot_reason != SHOT_REASON_SPAM)) {
        demo_override_arrow_hits(arrow);
    }
#endif // DEMO
}

static void animate_shots(Layer* layer, GContext* ctx) {
    for (size_t i = 0; i < MAX_ARROWS; i++) {  // TODO sort by distance
        ArrowContext* const arrow = &s_arrows[i];
        if (arrow_should_draw(arrow)) {
            switch(arrow->frame) {
            case 1:
                if (arrow->distance == ARROW_DISTANCE_UNINITIALISED) {
                    arrow_determine_accuracy(arrow);
                };
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
    layer_mark_dirty(s_layer_arrow);
}

static void animate_fall(Layer* layer, GContext* ctx) {
    bool any_still_falling = false;
    for (size_t i = 0; i < MAX_ARROWS; i++) {  // TODO sort by distance
        ArrowContext* const arrow = &s_arrows_falling[i];
        if (arrow_should_draw(arrow)) {
            // location
            arrow->offset_pos.x += arrow->velocity.x;
            arrow->offset_pos.y += arrow->velocity.y;

            // velocity
            arrow->velocity.y += (PBL_DISPLAY_WIDTH < 200) ? 1 : 2;  // gravity TODO change according to pebble's orientation
            if (ABS(arrow->velocity.x) > 2) {  // air resistance
                arrow->velocity.x -= SIGN(arrow->velocity.x);
            }

            // rotation; turn to point downwards (i.e. angle towards 0) with vertical air resistance
            if (arrow->velocity.y > 0) {
                const int32_t current_angle = arrow->angle + arrow->offset_angle;
                // Vertical air resistance scales with downward-velocity and horizontalness.
                const int32_t velocity_scalar = 8 * (arrow->velocity.y * arrow->velocity.y);
                // Sine gives us horizontalness and also the direction in which to turn.
                const int32_t turn = (velocity_scalar * sin_lookup(current_angle)) / TRIG_MAX_RATIO;
                if (ABSDIFF_WRAP(current_angle, DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(360)) > ABS(turn)) {
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

// Callback to render s_layer_arrow
static void arrow_canvas(Layer* layer, GContext* ctx) {
    animate_shots(layer, ctx);
    animate_fall(layer, ctx);
}


/******************************************************************************
 Handlers
******************************************************************************/

STATIC_ASSERT(LAST_ARROW_SHOT == HOUR_ARROW_INDEX);
// Shoot all indicator arrows, ending with the hour hand
static void shoot_indicator_arrows_for_time(struct tm *tick_time, TimeUnits units_changed, ShotReason shot_reason) {
    s_state.hour = tick_time->tm_hour % 12;
    s_state.min = tick_time->tm_min;
    s_state.sec = tick_time->tm_sec;

    int16_t delay = 1;

#if SECOND_HAND
    if (units_changed & SECOND_UNIT) {
        const int32_t angle = s_state.sec * (TRIG_MAX_ANGLE / SECONDS_PER_MINUTE);
        arrow_shoot(&s_arrows[SECOND_ARROW_INDEX], angle, ARROW_LENGTH_LONG, delay, shot_reason);
        delay++;
    }
#endif // SECOND_HAND
#if PBL_COMPASS
    if (shot_reason == SHOT_REASON_SHAKE) {
        CompassHeadingData compass = {0};
        if (compass_service_peek_logged(&compass)) {
            #if DEMO
                const int32_t angle = DEG_TO_TRIGANGLE(-45);
            #else // !DEMO
                const int32_t angle = TRIG_MAX_ANGLE - compass.true_heading;
            #endif // !DEMO
            delay = 0;
            arrow_shoot(&s_arrows[COMPASS_ARROW_INDEX], angle, ARROW_LENGTH_LONG, delay, SHOT_REASON_COMPASS);
            delay++;
        } else if (compass.compass_status == CompassStatusUnavailable) {
            LOG("ERROR: Compass service unavailable");
        } else {
            // TODO shoot a missed arrow across the screen
            compass_start_calibration();
        }
    }
#endif // PBL_COMPASS

    if (units_changed & MINUTE_UNIT) {
        const int32_t angle = s_state.min * (TRIG_MAX_ANGLE / MINUTES_PER_HOUR);
        arrow_shoot(&s_arrows[MINUTE_ARROW_INDEX], angle, ARROW_LENGTH_LONG, delay, shot_reason);
        delay++;

        { // hours
            const int32_t angle = (
                ((s_state.hour * MINUTES_PER_HOUR) + s_state.min)
                * (TRIG_MAX_ANGLE / (MINUTES_PER_DAY / 2))
            );
            arrow_shoot(&s_arrows[HOUR_ARROW_INDEX], angle, ARROW_LENGTH_SHORT, delay, shot_reason);
            delay++;
        }
    }
}

static void reshoot_indicator_arrows(ShotReason shot_reason) {
    // note we don't ever bother reshooting the second hand, since it does it by itself
    const time_t now = time(NULL);
    shoot_indicator_arrows_for_time(localtime(&now), MINUTE_UNIT | HOUR_UNIT, shot_reason);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    TRACE("tick_handler %d", units_changed);
#if !DEMO
    if (!s_notify_showing) {
        shoot_indicator_arrows_for_time(tick_time, units_changed, SHOT_REASON_TICK);
    }
#endif // !DEMO
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
    TRACE("accel_tap_handler");
    if (!achievement_dismiss() && !arrow_spam_stop()) {
        reshoot_indicator_arrows(SHOT_REASON_SHAKE);
    }
}


/******************************************************************************
 Main
******************************************************************************/

static void main_window_load(Window *window) {
    TRACE("main_window_load");

    Layer * const window_layer = window_get_root_layer(window);

    s_layer_target = layer_create(layer_get_frame(window_layer));
    layer_set_update_proc(s_layer_target, draw_target);
    layer_add_child(window_layer, s_layer_target);

    s_icon_trophy = gbitmap_create_with_resource(RESOURCE_ID_ICON_TROPHY);
    s_layer_trophy = layer_create(layer_get_frame(window_layer));
    layer_set_update_proc(s_layer_trophy, draw_trophies);
    layer_add_child(window_layer, s_layer_trophy);

    s_layer_arrow = layer_create(layer_get_frame(window_layer));
    layer_set_update_proc(s_layer_arrow, arrow_canvas);
    layer_add_child(window_layer, s_layer_arrow);

    // s_layer_trophy_notify
    trophy_notify_create(window_layer);
    layer_add_child(window_layer, (Layer*)s_layer_trophy_notify);

    // s_layer_status_text
    status_text_create(window_layer);
    layer_add_child(window_layer, (Layer*)s_layer_status_text);

    tick_timer_service_subscribe(SECOND_HAND ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
    accel_tap_service_subscribe(accel_tap_handler);

    achievements_load();
    (void)achievement_dismiss();

    reshoot_indicator_arrows(SHOT_REASON_INIT);

    s_initialising = false;
}

static void main_window_unload(Window *window) {
    TRACE("main_window_unload");

    gbitmap_destroy(s_icon_trophy);
    layer_destroy(s_layer_trophy);

    layer_destroy(s_layer_target);
    layer_destroy(s_layer_arrow);
    text_layer_destroy(s_layer_trophy_notify);
    text_layer_destroy(s_layer_status_text);

    tick_timer_service_unsubscribe();
    accel_tap_service_unsubscribe();
    compass_service_unsubscribe();  // compass_service_peek() automatically subscribes us
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
