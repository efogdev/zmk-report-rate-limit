#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/settings/settings.h>
#include "drivers/behavior_rate_limit_runtime.h"

#define DT_DRV_COMPAT zmk_rrl_shell
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_SHELL) && IS_ENABLED(CONFIG_ZMK_RRL_SHELL)
#define shprint(_sh, _fmt, ...) \
do { \
  if ((_sh) != NULL) \
    shell_print((_sh), _fmt, ##__VA_ARGS__); \
} while (0)

static int cmd_get_set(const struct shell *sh, const size_t argc, char **argv) {
    behavior_rate_limit_runtime_init();

    if (strcmp(argv[0], "get") == 0) {
        shprint(sh, "Sync window: %d msec", behavior_rate_limit_get_current_ms());
    } else if (argc == 2) {
        char *endptr;
        const uint8_t parsed = strtoul(argv[1], &endptr, 10);
        behavior_rate_limit_set_current_ms(parsed);
        shprint(sh, "Done!");
    } else {
        shprint(sh, "Usage: rrl <get|set> [value]");
    }

    return 0;
}

static int cmd_monitor(const struct shell *sh, const size_t argc, char **argv) {
    if (argc < 2) {
        shprint(sh, "Usage: rrl monitor <on|off>");
        return -EINVAL;
    }

    if (strcmp(argv[1], "on") == 0) {
        rrl_monitoring_set(true);
    } else if (strcmp(argv[1], "off") == 0) {
        rrl_monitoring_set(false);
    } else {
        shprint(sh, "Usage: rrl monitor <on|off>");
        return -EINVAL;
    }

    shprint(sh, "Done.");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_rrl,
    SHELL_CMD(get, NULL, "Get current report rate limit", cmd_get_set),
    SHELL_CMD(set, NULL, "Set report rate sync window", cmd_get_set),
    SHELL_CMD(monitor, NULL, "Monitor raw output values", cmd_monitor),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(rrl, &sub_rrl, "Report rate limiter", NULL);
#endif
