// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#if PBL_COMPASS
    #define PBL_IF_COMPASS_ELSE(a, b) a
#else // !PBL_COMPASS
    #define PBL_IF_COMPASS_ELSE(a, b) b
#endif // !PBL_COMPASS


#if PBL_COMPASS

#include <pebble.h>

Layer* compass_text_layer_create(Layer * parent);
void compass_text_layer_destroy(void);
bool compass_service_peek_logged(CompassHeadingData* data);
void compass_start_calibration(void);
void compass_stop_calibration(void);

#endif // PBL_COMPASS