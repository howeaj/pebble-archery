// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#include "macros.h"


#pragma pack(push, 1)  // prevent unpredictable format changes

// The names of these fields should match the messageKeys in config.json and package.json
typedef struct Config {
    bool enableCompass;
    bool enableAccel;
    bool showTrophies;
    bool enableSeconds;
    uint8_t reserved[96]; // Reserved for new values without needing to bump the version
} Config;
#define PERSIST_CONFIG_VERSION (1)
STATIC_ASSERT(sizeof(Config) == 100);

#pragma pack(pop)


typedef void (*NewConfigCallback)(void);

void config_init(NewConfigCallback callback);
void config_deinit(void);
const Config* config_get(void);
