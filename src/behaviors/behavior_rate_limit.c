#include <stdlib.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include "drivers/behavior.h"
#include "zephyr/logging/log.h"
#include "zephyr/settings/settings.h"
#include "zmk/behavior.h"
#include "drivers/behavior_rate_limit_runtime.h"
#if IS_ENABLED(CONFIG_ZMK_FEEDBACK_COMMON)
#include <zmk/feedback_common/feedback_gpio.h>
#endif

#define DT_DRV_COMPAT zmk_behavior_rate_limit
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int g_from_settings = -1;

struct behavior_rate_limit_config {
    const uint32_t feedback_duration;
    const uint8_t values_count;
    const int values_ms[CONFIG_ZIP_RATE_LIMIT_MAX_ARR_VALUES];
    const uint8_t feedback_wrap_pattern_len;
    const int feedback_wrap_pattern[CONFIG_ZIP_RATE_LIMIT_MAX_ARR_VALUES];
};

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata mtd_param1_values[] = {
    {
        .display_name = "Increase",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = 1,
    },
    {
        .display_name = "Decrease",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = -1,
    },
};

static const struct behavior_parameter_metadata_set profile_index_metadata_set = {
    .param1_values = mtd_param1_values,
    .param1_values_len = ARRAY_SIZE(mtd_param1_values),
};

static const struct behavior_parameter_metadata_set metadata_sets[] = {profile_index_metadata_set};
static const struct behavior_parameter_metadata metadata = { .sets_len = ARRAY_SIZE(metadata_sets), .sets = metadata_sets};
#endif

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static int on_zip_rrl_binding_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    const struct device* dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_rate_limit_config *cfg = dev->config;

    bool wrapped = false;
    if (cfg->values_count > 0) {
        const uint8_t current_ms = behavior_rate_limit_get_current_ms();
        int current_index = -1;

        for (int i = 0; i < cfg->values_count; i++) {
            if (cfg->values_ms[i] == current_ms) {
                current_index = i;
                break;
            }
        }

        int next_index = (current_index + 1) % cfg->values_count;
        if (current_index == -1) {
            next_index = 0;
        } else if (next_index == 0) {
            wrapped = true;
            LOG_DBG("Rate limit wrapped around");
        }

        const uint8_t next_ms = cfg->values_ms[next_index];
        behavior_rate_limit_set_current_ms(next_ms);
        LOG_INF("Rate limit changed from %d ms to %d ms", current_ms, next_ms);
    } else {
        LOG_WRN("No rate limit values defined for cycling");
    }

#if IS_ENABLED(CONFIG_ZMK_FEEDBACK_COMMON)
    if (cfg->feedback_duration > 0) {
        if (wrapped && cfg->feedback_wrap_pattern_len > 0) {
            fbc_trigger_pattern(cfg->feedback_wrap_pattern, cfg->feedback_wrap_pattern_len);
        } else {
            fbc_trigger(cfg->feedback_duration);
        }
    }
#else
    ARG_UNUSED(wrapped);
#endif

    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_rate_limit_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api behavior_rate_limit_driver_api = {
    .binding_pressed = on_zip_rrl_binding_pressed,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define ZIP_RRL_INST(n)                                                                                  \
    static const struct behavior_rate_limit_config behavior_rate_limit_config_##n = {                    \
        .feedback_duration = DT_INST_PROP_OR(n, feedback_duration, 0),                                   \
        .values_count = DT_INST_PROP_LEN(n, values_ms),                                                  \
        .values_ms = DT_INST_PROP_OR(n, values_ms, { 0 }}),                                              \
        .feedback_wrap_pattern_len = DT_INST_PROP_LEN_OR(n, feedback_wrap_pattern, 0),                   \
        .feedback_wrap_pattern = DT_INST_PROP_OR(n, feedback_wrap_pattern, { 0 }}),                      \
    };                                                                                                   \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_rate_limit_init, NULL, NULL,                                     \
        &behavior_rate_limit_config_##n, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_rate_limit_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ZIP_RRL_INST)

#if IS_ENABLED(CONFIG_SETTINGS)
// ReSharper disable once CppParameterMayBeConst
static int zip_rrl_settings_load_cb(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const int err = read_cb(cb_arg, &g_from_settings, sizeof(g_from_settings));
    if (err < 0) {
        LOG_ERR("Failed to load settings (err = %d)", err);
    }

    behavior_rate_limit_set_current_ms(g_from_settings);
    return err;
}

SETTINGS_STATIC_HANDLER_DEFINE(zip_rrl_cycle, ZIP_RRL_SETTINGS_PREFIX, NULL, zip_rrl_settings_load_cb, NULL, NULL);
#endif

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
