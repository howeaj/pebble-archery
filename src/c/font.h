// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// Multiplatform font definitions

#pragma once

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
