// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// Compass reading and calibration

#if PBL_COMPASS

#include "compass.h"

#include "font.h"
#include "macros.h"
#include "misc.h"


static TextLayer* s_layer_status_text;

#define COMPASS_CALIB_POLL_RATE_MS (2000)  // how often to check progress while calibrating. Must be < PEEK_TIMEOUT_MS.

static void show_status_message(bool show){
    static AnimateScrollState state = AnimateScrollState_Init;
    animate_scroll((Layer*)s_layer_status_text, show, true, &state);
}

Layer* compass_text_layer_create(Layer * parent) {
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

    return (Layer*)s_layer_status_text;
}

void compass_text_layer_destroy(void) {
    text_layer_destroy(s_layer_status_text);

    compass_service_unsubscribe();  // compass_service_peek() automatically subscribes us
}

// return true on success
bool compass_service_peek_logged(CompassHeadingData* data) {
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

void compass_start_calibration(void) {
    LOG("Starting compass calibration");
    text_layer_set_text(s_layer_status_text, "move wrist to calibrate compass");
    show_status_message(true);
    app_timer_register(COMPASS_CALIB_POLL_RATE_MS, compass_calibrate_callback, NULL);
}


#endif // PBL_COMPASS