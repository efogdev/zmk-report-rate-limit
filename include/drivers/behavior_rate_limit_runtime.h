#pragma once
#define ZIP_RRL_SETTINGS_PREFIX "zip_rrl_sens"

void behavior_rate_limit_runtime_init();
uint8_t behavior_rate_limit_get_current_ms();
void behavior_rate_limit_set_current_ms(uint8_t value);
