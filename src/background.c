#include "background.h"

#include "app_state.h"
#include "devices/protocol.h"
#include "service.h"
#include "tray.h"

#include <hidapi/hidapi.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static AppState *running_state;

static void stop_signal(int signal_id) {
    (void)signal_id;
    if (!running_state) return;
    g_mutex_lock(&running_state->lock);
    running_state->stop_requested = true;
    g_mutex_unlock(&running_state->lock);
}

int background_run(void) {
    if (geteuid() != 0) {
        fprintf(stderr, "DeepCool Digital Linux background mode must be run with sudo/root to access HID devices correctly.\n");
        return 1;
    }

    AppState state;
    memset(&state, 0, sizeof(state));
    g_mutex_init(&state.lock);
    state.config.update_ms = 1000;
    state.config.auto_interval_ms = AUTO_MODE_INTERVAL_MS;
    state.config.device_index = 0;
    state.config.gpu_index = -1;
    state.device_count = scan_devices(state.devices, MAX_DEVICES);
    state.gpu_count = scan_gpus(state.gpus, MAX_GPUS);
    if (state.gpu_count > 0) state.config.gpu_index = 0;
    app_config_load(&state);

    if (state.device_count == 0) {
        fprintf(stderr, "No DeepCool device found\n");
        hid_exit();
        return 1;
    }
    if (state.config.device_index < 0 || (size_t)state.config.device_index >= state.device_count) state.config.device_index = 0;
    if (state.config.mode == MODE_DEFAULT) {
        state.config.mode = effective_mode(state.devices[state.config.device_index].series, MODE_DEFAULT);
    }

    running_state = &state;
    signal(SIGINT, stop_signal);
    signal(SIGTERM, stop_signal);

    TrayIcon *tray = tray_icon_start();

    GApplication *notifier = g_application_new("io.github.deepcool.digital.linux.background", G_APPLICATION_DEFAULT_FLAGS);
    if (g_application_register(notifier, NULL, NULL) && g_application_get_dbus_connection(notifier)) {
        GNotification *notification = g_notification_new("DeepCool Digital");
        g_notification_set_body(notification, "Running in background");
        g_application_send_notification(notifier, "deepcool-digital-running", notification);
        g_object_unref(notification);
    }

    g_mutex_lock(&state.lock);
    state.running = true;
    g_mutex_unlock(&state.lock);
    device_worker_main(&state);
    tray_icon_stop(tray);
    if (notifier) g_object_unref(notifier);
    hid_exit();
    return 0;
}
