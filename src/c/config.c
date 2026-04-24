// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// App configuration via clay

#include "config.h"

#include <pebble.h>

#include "debug.h"
#include "macros.h"
#include "persist_keys.h"


static Config s_config = {0};
static NewConfigCallback s_new_config_callback = NULL;


// This should match the defaults in config.json
static const Config default_config = {
    .enableCompass = true,
    .enableAccel = true,
    .showTrophies = true,
    .enableSeconds = false
};


/******************************************************************************
 Local watch persistence
******************************************************************************/

static bool is_local_persist_written_and_current_version(void) {
    return persist_read_int(PERSIST_KEY_CONFIG_VERSION) == PERSIST_CONFIG_VERSION;
}

/// Return true if config was loaded
static bool local_persist_load(void) {
    StatusCode status = E_DOES_NOT_EXIST;
    if (is_local_persist_written_and_current_version()){
        status = persist_read_data(PERSIST_KEY_CONFIG, &s_config, sizeof(s_config));
        ASSERT(status == sizeof(s_config));
    }
    return status == sizeof(s_config);
}

static void local_persist_save(void) {
    StatusCode status = persist_write_data(PERSIST_KEY_CONFIG, &s_config, sizeof(s_config));
    ASSERT(status == sizeof(s_config));

    if (status == sizeof(s_config)) {
        status = persist_write_int(PERSIST_KEY_CONFIG_VERSION, PERSIST_CONFIG_VERSION);
        ASSERT(status == sizeof(int32_t));
    }
}


/******************************************************************************
 Receive config from phone
******************************************************************************/

#define RECEIVE_CONFIG_BOOL(message_key) MACRO_START \
    const Tuple *tuple = dict_find(iter, MESSAGE_KEY_##message_key); \
    if (tuple) { \
        s_config.message_key = (tuple->value->int32 == 1); \
    } else { \
        LOG("Missing config: " #message_key); \
    } \
MACRO_END

#define RECEIVE_CONFIG_COLOR(message_key) MACRO_START \
    const Tuple *tuple = dict_find(iter, MESSAGE_KEY_##message_key); \
    if (tuple) { \
        s_config.message_key = GColorFromHEX(tuple->value->int32); \
    } else { \
        LOG("Missing config: " #message_key); \
    } \
MACRO_END

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
    Config saved_config = s_config;

    RECEIVE_CONFIG_BOOL(enableCompass);
    RECEIVE_CONFIG_BOOL(enableAccel);
    RECEIVE_CONFIG_BOOL(showTrophies);
    RECEIVE_CONFIG_BOOL(enableSeconds);

    if (memcmp(&saved_config, &s_config, sizeof(saved_config)) != 0) {
        LOG("New app config received");
        local_persist_save();
        if (s_new_config_callback != NULL) {
            s_new_config_callback();
        }
    }
}


/******************************************************************************
 Public methods
******************************************************************************/

// `callback` is an optional function to call whenever a new config is received from the phone;
// it should be used to mark affected layers dirty or otherwise live-update config changes
void config_init(NewConfigCallback callback) {
    s_new_config_callback = callback;
    if (!local_persist_load()) {
        s_config = default_config;
    }
    app_message_register_inbox_received(&inbox_received_handler);
    app_message_open(128, 128);  // TODO how big?
}

void config_deinit(void) {
    app_message_deregister_callbacks();
}

const Config* config_get(void) {
    return &s_config;
}