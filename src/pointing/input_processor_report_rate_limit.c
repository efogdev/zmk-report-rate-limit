/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_report_rate_limit

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/input_processor.h>
#include <zephyr/sys/util.h> // CLAMP
#include <drivers/behavior_rate_limit_runtime.h>

// define HAS_BLE_VIA_USB for shield has both BLE and USB
#define HAS_BLE_VIA_USB (IS_ENABLED(CONFIG_ZMK_USB) && IS_ENABLED(CONFIG_ZMK_BLE))

#if IS_ENABLED(CONFIG_SETTINGS)
#ifndef CONFIG_SETTINGS_RUNTIME
#define CONFIG_SETTINGS_RUNTIME true
#endif
#include <zephyr/settings/settings.h>
#endif

#if HAS_BLE_VIA_USB
#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#include <zmk/events/endpoint_changed.h>
#endif // HAS_BLE_VIA_USB

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/keymap.h>
#define MAX_LEN 4

static uint8_t g_delay = CONFIG_ZMK_INPUT_PROCESSOR_REPORT_RATE_LIMIT_DEFAULT;
static bool g_monitor = false, g_abs = false;

#if IS_ENABLED(CONFIG_SETTINGS)
struct k_work_delayable zip_rrl_save_work;
static void zip_rrl_save_work_cb(struct k_work *work);
#endif

struct zip_rrl_config {
    uint8_t type;
    bool limit_ble_only;
    size_t codes_len;
    uint16_t codes[];
};

struct zip_rrl_data {
#if HAS_BLE_VIA_USB
    bool active;
#endif // HAS_BLE_VIA_USB
    int16_t rmds[MAX_LEN];
    bool syncs[MAX_LEN];
    int64_t last_rpt[MAX_LEN];
};

static void zip_rrl_reset_rmds(const struct device *dev) {
    struct zip_rrl_data *data = dev->data;
    const int64_t now = k_uptime_get();
    for (int i = 0; i < MAX_LEN; i++) {
        data->rmds[i] = 0;
        data->syncs[i] = false;
        data->last_rpt[i] = now;
    }
}

#if HAS_BLE_VIA_USB
static void zip_rrl_set_active(const struct device *dev, const bool active) {
    LOG_DBG("set zip report rate limit to %s", active ? "active" : "inactive");
    struct zip_rrl_data *data = dev->data;
    data->active = active;
    if (!active) {
        zip_rrl_reset_rmds(dev);
    }
}

#define GET_ZIP_RRL_DEV(node_id) DEVICE_DT_GET(node_id),

static const struct device *zip_rrl_devs[] = {
    DT_FOREACH_STATUS_OKAY(DT_DRV_COMPAT, GET_ZIP_RRL_DEV)
};

static int zip_rrl_profile_listener(const zmk_event_t *eh) {
    const struct zmk_endpoint_changed *ep_changed = as_zmk_endpoint_changed(eh);
    if (ep_changed) {
        const struct zmk_endpoint_instance ep = zmk_endpoints_selected();
        for (size_t i = 0; i < ARRAY_SIZE(zip_rrl_devs); i++) {
            const struct zip_rrl_config *cfg = zip_rrl_devs[i]->config;
            if (cfg->limit_ble_only) {
                zip_rrl_set_active(zip_rrl_devs[i], ep.transport == ZMK_TRANSPORT_BLE);
            }
        }
    }
    return 0;
}

ZMK_LISTENER(zip_rrl_profile_listener, zip_rrl_profile_listener);
ZMK_SUBSCRIPTION(zip_rrl_profile_listener, zmk_endpoint_changed);
#endif // HAS_BLE_VIA_USB

static void monitor(const struct input_event *event) {
    static char* name;
    static uint8_t i = 0;
    if (g_monitor) {
        if (event->code == INPUT_REL_X) {
            name = "X";
        } else if (event->code == INPUT_REL_Y) {
            name = "Y";
        } else if (event->code == INPUT_REL_WHEEL) {
            name = "SCROLL";
        } else if (event->code == INPUT_REL_HWHEEL) {
            name = "H_SCROLL";
        } else {
            name = "UNKNOWN";
        }

        printf("(%s = %d) ", name, g_abs ? abs(event->value) : event->value);
        if (i++ > 4) {
            printf("\n");
            i = 0;
        }
    }
}

static int limit_val(const struct device *dev, struct input_event *event,
                     const int code_idx, const uint32_t delay_ms,
                     struct zmk_input_processor_state *state) {
    struct zip_rrl_data *data = dev->data;
    const int64_t now = k_uptime_get();

    if (now - data->last_rpt[code_idx] >= delay_ms * MAX_LEN) {
        data->rmds[code_idx] = 0;
        data->syncs[code_idx] = false;
    }

    if (now - data->last_rpt[code_idx] < delay_ms) {
        data->rmds[code_idx] = CLAMP(data->rmds[code_idx] + event->value, INT16_MIN, INT16_MAX);
        data->syncs[code_idx] |= event->sync;
        event->value = 0;
        event->sync = false;
        LOG_DBG("Accumulated a value, rate limited");
        return ZMK_INPUT_PROC_STOP;
    }

    event->value = CLAMP(event->value + data->rmds[code_idx], INT32_MIN, INT32_MAX);
    event->sync |= data->syncs[code_idx];

    data->rmds[code_idx] = 0;
    data->syncs[code_idx] = false;
    data->last_rpt[code_idx] = now;

    monitor(event);
    return ZMK_INPUT_PROC_CONTINUE;
}

static int zip_rrl_handle_event(const struct device *dev, struct input_event *event, 
                                uint32_t param1, uint32_t param2, 
                                struct zmk_input_processor_state *state) {
#if HAS_BLE_VIA_USB
    const struct zip_rrl_data *data = dev->data;
    if (!data->active) {
        monitor(event);
        return ZMK_INPUT_PROC_CONTINUE;
    }
#endif // HAS_BLE_VIA_USB

    const struct zip_rrl_config *cfg = dev->config;
    if (event->type != cfg->type) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    for (int i = 0; i < cfg->codes_len; i++) {
        if (cfg->codes[i] == event->code) {
            return limit_val(dev, event, i, g_delay, state);
        }
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api sy_driver_api = {
    .handle_event = zip_rrl_handle_event,
};

static int zip_rrl_init(const struct device *dev) {
    zip_rrl_reset_rmds(dev);

#if HAS_BLE_VIA_USB
    zip_rrl_set_active(dev, true);
#endif // HAS_BLE_VIA_USB

    behavior_rate_limit_runtime_init();
    return 0;
}

#if IS_ENABLED(CONFIG_SETTINGS)
static void zip_rrl_save_work_cb(struct k_work *work) {
    const int err = settings_save_one(ZIP_RRL_SETTINGS_PREFIX, &g_delay, sizeof(g_delay));
    if (err < 0) {
        LOG_ERR("Failed to save rate limit %d", err);
    } else {
        LOG_DBG("Rate limit saved");
    }
}

static void zip_rrl_save() {
    k_work_reschedule(&zip_rrl_save_work, K_MSEC(CONFIG_ZIP_RRL_SETTINGS_SAVE_DELAY));
}
#endif

void behavior_rate_limit_runtime_init() {
#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_init_delayable(&zip_rrl_save_work, zip_rrl_save_work_cb);
#endif
}

uint8_t behavior_rate_limit_get_current_ms() {
    return g_delay;
}

void behavior_rate_limit_set_current_ms(const uint8_t value) {
    g_delay = value;

#if IS_ENABLED(CONFIG_SETTINGS)
    zip_rrl_save();
#endif
}

void rrl_monitoring_set(const bool enabled, const bool abs) {
    g_monitor = enabled;
    if (g_monitor) {
        g_abs = abs;
    }
}

#define RRL_INST(n)                                                                            \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, codes)                                                    \
                 <= MAX_LEN,                \
                 "Codes length > MAX_LEN"); \
    static struct zip_rrl_data data_##n = {};                                                  \
    static struct zip_rrl_config config_##n = {                                                \
        .type = DT_INST_PROP_OR(n, type, INPUT_EV_REL),                                        \
        .limit_ble_only = DT_INST_PROP(n, limit_ble_only),                                     \
        .codes_len = DT_INST_PROP_LEN(n, codes),                                               \
        .codes = DT_INST_PROP(n, codes),                                                       \
    };                                                                                         \
    DEVICE_DT_INST_DEFINE(n, &zip_rrl_init, NULL, &data_##n, &config_##n, POST_KERNEL,         \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sy_driver_api);

// ToDo refactor to ACTUALLY support multiple instances?
DT_INST_FOREACH_STATUS_OKAY(RRL_INST)

#if IS_ENABLED(CONFIG_SETTINGS)
// ReSharper disable once CppParameterMayBeConst
static int zip_rrl_settings_load_cb(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const int err = read_cb(cb_arg, &g_delay, sizeof(g_delay));
    if (err < 0) {
        LOG_ERR("Failed to load settings (err = %d)", err);
    }

    return err;
}

SETTINGS_STATIC_HANDLER_DEFINE(zip_rrl_settings, ZIP_RRL_SETTINGS_PREFIX, NULL, zip_rrl_settings_load_cb, NULL, NULL);
#endif

