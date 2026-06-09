#include "service.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char *config_dir(void) {
    return g_build_filename(g_get_user_config_dir(), "deepcool-digital-linux", NULL);
}

static char *config_path(void) {
    const char *override = g_getenv("DEEPCOOL_CONFIG");
    if (override && override[0]) return g_strdup(override);
    char *dir = config_dir();
    char *path = g_build_filename(dir, "config.ini", NULL);
    g_free(dir);
    return path;
}

static bool ensure_parent_dir(const char *path, char *error, size_t error_len) {
    if (g_mkdir_with_parents(path, 0755) == 0) return true;
    snprintf(error, error_len, "Failed to create %s: %s", path, g_strerror(errno));
    return false;
}

static bool run_command(const char *command, char *error, size_t error_len) {
    int status = 0;
    char *stderr_text = NULL;
    gboolean ok = g_spawn_command_line_sync(command, NULL, &stderr_text, &status, NULL);
    if (ok && status == 0) {
        g_free(stderr_text);
        return true;
    }
    snprintf(error, error_len, "%s", stderr_text && stderr_text[0] ? stderr_text : "systemctl command failed");
    g_free(stderr_text);
    return false;
}

static bool run_pkexec_shell(const char *script, char *error, size_t error_len) {
    char *quoted = g_shell_quote(script);
    char *command = g_strdup_printf("pkexec /bin/sh -c %s", quoted);
    bool ok = run_command(command, error, error_len);
    g_free(command);
    g_free(quoted);
    return ok;
}

bool app_config_save(AppState *state, char *error, size_t error_len) {
    char *dir = config_dir();
    if (!ensure_parent_dir(dir, error, error_len)) {
        g_free(dir);
        return false;
    }
    g_free(dir);

    GKeyFile *key = g_key_file_new();
    int device_index = state->config.device_index;
    if (device_index >= 0 && (size_t)device_index < state->device_count) {
        g_key_file_set_integer(key, "device", "pid", state->devices[device_index].pid);
    }
    int gpu_index = state->config.gpu_index;
    if (gpu_index >= 0 && (size_t)gpu_index < state->gpu_count) {
        g_key_file_set_string(key, "gpu", "address", state->gpus[gpu_index].address);
    }
    g_key_file_set_string(key, "display", "mode", mode_symbol(state->config.mode));
    g_key_file_set_string(key, "display", "secondary", mode_symbol(state->config.secondary));
    g_key_file_set_integer(key, "display", "update_ms", state->config.update_ms);
    g_key_file_set_integer(key, "display", "auto_interval_ms", state->config.auto_interval_ms);
    g_key_file_set_boolean(key, "display", "fahrenheit", state->config.fahrenheit);
    g_key_file_set_boolean(key, "display", "alarm", state->config.alarm);
    g_key_file_set_integer(key, "display", "rotate", state->config.rotate);
    g_key_file_set_boolean(key, "display", "lead_zeros", state->config.lead_zeros);

    gsize len = 0;
    char *data = g_key_file_to_data(key, &len, NULL);
    char *path = config_path();
    bool ok = g_file_set_contents(path, data, len, NULL);
    if (!ok) snprintf(error, error_len, "Failed to write %s", path);
    g_free(path);
    g_free(data);
    g_key_file_unref(key);
    return ok;
}

bool app_config_load(AppState *state) {
    char *path = config_path();
    GKeyFile *key = g_key_file_new();
    bool loaded = g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL);
    g_free(path);
    if (!loaded) {
        g_key_file_unref(key);
        return false;
    }

    int pid = g_key_file_get_integer(key, "device", "pid", NULL);
    for (size_t i = 0; i < state->device_count; i++) {
        if (state->devices[i].pid == pid) {
            state->config.device_index = (int)i;
            break;
        }
    }

    char *gpu_address = g_key_file_get_string(key, "gpu", "address", NULL);
    if (gpu_address) {
        for (size_t i = 0; i < state->gpu_count; i++) {
            if (g_strcmp0(state->gpus[i].address, gpu_address) == 0) {
                state->config.gpu_index = (int)i;
                break;
            }
        }
        g_free(gpu_address);
    }

    char *mode = g_key_file_get_string(key, "display", "mode", NULL);
    char *secondary = g_key_file_get_string(key, "display", "secondary", NULL);
    state->config.mode = mode_from_symbol(mode);
    state->config.secondary = mode_from_symbol(secondary);
    state->config.update_ms = (uint32_t)g_key_file_get_integer(key, "display", "update_ms", NULL);
    if (state->config.update_ms < 100 || state->config.update_ms > 2000) state->config.update_ms = 1000;
    state->config.auto_interval_ms = (uint32_t)g_key_file_get_integer(key, "display", "auto_interval_ms", NULL);
    if (state->config.auto_interval_ms < 1000 || state->config.auto_interval_ms > 30000) state->config.auto_interval_ms = AUTO_MODE_INTERVAL_MS;
    state->config.fahrenheit = g_key_file_get_boolean(key, "display", "fahrenheit", NULL);
    state->config.alarm = g_key_file_get_boolean(key, "display", "alarm", NULL);
    state->config.rotate = (uint16_t)g_key_file_get_integer(key, "display", "rotate", NULL);
    state->config.lead_zeros = g_key_file_get_boolean(key, "display", "lead_zeros", NULL);
    g_free(mode);
    g_free(secondary);
    g_key_file_unref(key);
    return true;
}

bool service_install_enable(AppState *state, char *error, size_t error_len) {
    if (!app_config_save(state, error, error_len)) return false;

    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) {
        snprintf(error, error_len, "Failed to resolve executable path");
        return false;
    }
    exe[n] = 0;

    char *cfg_path = config_path();
    const char *session_bus = g_getenv("DBUS_SESSION_BUS_ADDRESS");
    const char *runtime_dir = g_getenv("XDG_RUNTIME_DIR");
    char *session_env = session_bus && session_bus[0] ? g_strdup_printf("Environment=DBUS_SESSION_BUS_ADDRESS=%s\n", session_bus) : g_strdup("");
    char *runtime_env = runtime_dir && runtime_dir[0] ? g_strdup_printf("Environment=XDG_RUNTIME_DIR=%s\n", runtime_dir) : g_strdup("");

    char *unit = g_strdup_printf(
        "[Unit]\n"
        "Description=DeepCool Digital background service\n"
        "After=multi-user.target\n\n"
        "[Service]\n"
        "Type=simple\n"
        "Environment=DEEPCOOL_CONFIG=%s\n"
        "%s"
        "%s"
        "ExecStart=%s --background\n"
        "Restart=on-failure\n"
        "RestartSec=5s\n\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n",
        cfg_path,
        session_env,
        runtime_env,
        exe);
    char *tmp_path = g_strdup_printf("%s/deepcool-digital.service.tmp", g_get_tmp_dir());
    bool ok = g_file_set_contents(tmp_path, unit, -1, NULL);
    g_free(cfg_path);
    g_free(session_env);
    g_free(runtime_env);
    g_free(unit);
    if (!ok) {
        g_free(tmp_path);
        snprintf(error, error_len, "Failed to write temporary deepcool-digital.service");
        return false;
    }

    char *tmp_quoted = g_shell_quote(tmp_path);
    char *script = g_strdup_printf(
        "cp %s /etc/systemd/system/deepcool-digital.service && "
        "systemctl daemon-reload && "
        "systemctl enable --now deepcool-digital.service",
        tmp_quoted);
    ok = run_pkexec_shell(script, error, error_len);
    g_free(script);
    g_free(tmp_quoted);
    g_free(tmp_path);
    return ok;
}

bool service_disable(char *error, size_t error_len) {
    return run_pkexec_shell(
        "systemctl disable --now deepcool-digital.service; "
        "rm -f /etc/systemd/system/deepcool-digital.service; "
        "systemctl daemon-reload",
        error,
        error_len);
}

bool service_is_enabled(void) {
    char error[256];
    return run_command("systemctl is-enabled deepcool-digital.service", error, sizeof(error));
}

bool service_is_active(void) {
    char error[256];
    return run_command("systemctl is-active deepcool-digital.service", error, sizeof(error));
}
