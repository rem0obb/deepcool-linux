#ifndef DEEPCOOL_SERVICE_H
#define DEEPCOOL_SERVICE_H

#include "app_state.h"

#include <stdbool.h>

bool app_config_save(AppState *state, char *error, size_t error_len);
bool app_config_load(AppState *state);
bool service_install_enable(AppState *state, char *error, size_t error_len);
bool service_disable(char *error, size_t error_len);
bool service_is_enabled(void);
bool service_is_active(void);

#endif
