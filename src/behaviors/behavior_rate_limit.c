#include <stdlib.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include "drivers/behavior.h"
#include "zephyr/drivers/gpio.h"
#include "zephyr/logging/log.h"
#include "zephyr/settings/settings.h"
#include "zmk/behavior.h"
#include "drivers/behavior_rate_limit_runtime.h"

#define DT_DRV_COMPAT zmk_behavior_rate_limit
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static uint8_t g_dev_num = 0;
static const char* g_devices[CONFIG_ZIP_RATE_LIMIT_MAX_DEVICES] = { NULL };
static int g_from_settings = -1;

struct behavior_rate_limit_config {
    const struct gpio_dt_spec feedback_gpios;
    const struct gpio_dt_spec feedback_extra_gpios;
    const uint32_t feedback_duration;
    const uint8_t values_count;
    const int values_ms[CONFIG_ZIP_RATE_LIMIT_MAX_ARR_VALUES];
    const uint8_t feedback_wrap_pattern_len;
    const int feedback_wrap_pattern[CONFIG_ZIP_RATE_LIMIT_MAX_ARR_VALUES];
};

struct behavior_rate_limit_data {
    const struct device *dev;
    struct k_work_delayable feedback_off_work;
    struct k_work_delayable feedback_pattern_work;
    int previous_feedback_extra_state;
    uint8_t current_pattern_index;
    bool pattern_active;
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
    struct behavior_rate_limit_data *data = dev->data;

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

    if (cfg->feedback_duration > 0 && cfg->feedback_gpios.port != NULL) {
        if (cfg->feedback_extra_gpios.port != NULL) {
            data->previous_feedback_extra_state = gpio_pin_get_dt(&cfg->feedback_extra_gpios);
            gpio_pin_set_dt(&cfg->feedback_extra_gpios, 1);
        }

        if (wrapped && cfg->feedback_wrap_pattern_len > 0) {
            data->pattern_active = true;
            data->current_pattern_index = 0;
            
            if (cfg->feedback_wrap_pattern_len > 0) {
                const int pattern_duration = cfg->feedback_wrap_pattern[0];
                
                if (gpio_pin_set_dt(&cfg->feedback_gpios, 1) == 0) {
                    data->current_pattern_index = 1;
                    k_work_reschedule(&data->feedback_pattern_work, K_MSEC(pattern_duration));
                    LOG_DBG("Starting feedback wrap pattern: duration=%d",
                            pattern_duration);
                } else {
                    LOG_ERR("Failed to enable the feedback pattern");
                    data->pattern_active = false;
                }
            }
        } else {
            if (gpio_pin_set_dt(&cfg->feedback_gpios, 1) == 0) {
                k_work_reschedule(&data->feedback_off_work, K_MSEC(cfg->feedback_duration));
            } else {
                LOG_ERR("Failed to enable the feedback");
            }
        }
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static void feedback_off_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    const struct behavior_rate_limit_data *data = CONTAINER_OF(dwork, struct behavior_rate_limit_data, feedback_off_work);
    const struct device *dev = data->dev;
    const struct behavior_rate_limit_config *config = dev->config;

    if (data->pattern_active) {
        if (config->feedback_gpios.port != NULL) {
            gpio_pin_set_dt(&config->feedback_gpios, 0);
        }

        LOG_DBG("Feedback pattern step completed");
        return;
    }

    if (config->feedback_extra_gpios.port != NULL) {
        gpio_pin_set_dt(&config->feedback_extra_gpios, data->previous_feedback_extra_state);
    }

    if (config->feedback_gpios.port != NULL) {
        gpio_pin_set_dt(&config->feedback_gpios, 0);
    }

    LOG_DBG("Feedback turned off, extra GPIOs restored to previous state");
}

static void feedback_pattern_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct behavior_rate_limit_data *data = CONTAINER_OF(dwork, struct behavior_rate_limit_data, feedback_pattern_work);
    const struct device *dev = data->dev;
    const struct behavior_rate_limit_config *config = dev->config;

    if (!data->pattern_active) {
        return;
    }

    if (data->current_pattern_index >= config->feedback_wrap_pattern_len) {
        data->pattern_active = false;
        
        if (config->feedback_extra_gpios.port != NULL) {
            gpio_pin_set_dt(&config->feedback_extra_gpios, data->previous_feedback_extra_state);
        }
        
        if (config->feedback_gpios.port != NULL) {
            gpio_pin_set_dt(&config->feedback_gpios, 0);
        }
        
        LOG_DBG("Feedback pattern completed");
        return;
    }

    const int pattern_duration = config->feedback_wrap_pattern[data->current_pattern_index];
    const int pin_state = (data->current_pattern_index % 2 == 1) ? 0 : 1;
    
    if (config->feedback_gpios.port != NULL) {
        gpio_pin_set_dt(&config->feedback_gpios, pin_state);
    }
    
    LOG_DBG("Feedback pattern step %d: state=%d, duration=%d",
            data->current_pattern_index, pin_state, pattern_duration);
    
    data->current_pattern_index++;
    k_work_reschedule(&data->feedback_pattern_work, K_MSEC(pattern_duration));
}

static int behavior_rate_limit_init(const struct device *dev) {
    const struct behavior_rate_limit_config *cfg = dev->config;
    struct behavior_rate_limit_data *data = dev->data;
    data->previous_feedback_extra_state = 0;
    data->pattern_active = false;
    data->current_pattern_index = 0;
    
    if (cfg->feedback_gpios.port != NULL) {
        if (gpio_pin_configure_dt(&cfg->feedback_gpios, GPIO_OUTPUT) != 0) {
            LOG_WRN("Failed to configure rate limit feedback GPIO");
        } else {
            LOG_DBG("Rate limit feedback GPIO configured");
        }

        k_work_init_delayable(&data->feedback_off_work, feedback_off_work_cb);
        k_work_init_delayable(&data->feedback_pattern_work, feedback_pattern_work_cb);
    } else {
        LOG_DBG("No feedback set up for rate limit cycling");
    }

    if (cfg->feedback_extra_gpios.port != NULL) {
        if (gpio_pin_configure_dt(&cfg->feedback_extra_gpios, GPIO_OUTPUT) != 0) {
            LOG_WRN("Failed to configure rate limit extra feedback GPIO");
        } else {
            LOG_DBG("Rate limit extra feedback GPIO configured");
        }
    } else {
        LOG_DBG("No extra feedback set up for rate limit cycling");
    }

    data->dev = dev;
    g_devices[g_dev_num++] = dev->name;
    return 0;
}

static const struct behavior_driver_api behavior_rate_limit_driver_api = {
    .binding_pressed = on_zip_rrl_binding_pressed,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define ZIP_RRL_INST(n)                                                                                  \
    static struct behavior_rate_limit_data behavior_rate_limit_data_##n = {};                            \
    static const struct behavior_rate_limit_config behavior_rate_limit_config_##n = {                    \
        .feedback_gpios = GPIO_DT_SPEC_INST_GET_OR(n, feedback_gpios, { .port = NULL }),                 \
        .feedback_extra_gpios = GPIO_DT_SPEC_INST_GET_OR(n, feedback_extra_gpios, { .port = NULL }),     \
        .feedback_duration = DT_INST_PROP_OR(n, feedback_duration, 0),                                   \
        .values_count = DT_INST_PROP_LEN(n, values_ms),                                                  \
        .values_ms = DT_INST_PROP_OR(n, values_ms, { 0 }}),                                              \
        .feedback_wrap_pattern_len = DT_INST_PROP_LEN_OR(n, feedback_wrap_pattern, 0),                   \
        .feedback_wrap_pattern = DT_INST_PROP_OR(n, feedback_wrap_pattern, { 0 }}),                      \
    };                                                                                                   \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_rate_limit_init, NULL, &behavior_rate_limit_data_##n,            \
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
