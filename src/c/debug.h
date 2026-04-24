// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// Debug options. Everything should be false for release builds!

#pragma once

#include <pebble.h>

#define DEMO false
#define DEBUG false
#define DISABLE_VIBE false
#define FORCE_COMPASS (DEMO || false)  // fake compass calibrated
#define FORCE_LUCK false  // always hit centre
#define FORCE_BACKLIGHT_ON (DEMO || false)
#define FORCE_BLUETOOTH_DISCONNECT false  // because emu-bt-connection takes forever

#if DEMO
    #define IF_DEMO_ELSE(is_demo, not_demo) (is_demo)
#else  // !DEMO
    #define IF_DEMO_ELSE(is_demo, not_demo) (not_demo)
#endif  // !DEMO

#if FORCE_BACKLIGHT_ON
    #define DEMO_BACKLIGHT_ENABLE(on) light_enable(on)
#else // !FORCE_BACKLIGHT_ON
    #define DEMO_BACKLIGHT_ENABLE(on)
#endif // !FORCE_BACKLIGHT_ON
