#include "app_state.h"
#include "background.h"
#include "tray.h"
#include "ui/ui.h"

#include <hidapi/hidapi.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int relaunch_with_pkexec(int argc, char **argv) {
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) {
        fprintf(stderr, "Failed to resolve executable path. Run with sudo.\n");
        return 1;
    }
    exe[n] = 0;

    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(args, g_strdup("pkexec"));
    g_ptr_array_add(args, g_strdup("env"));

    const char *env_names[] = {"DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_RUNTIME_DIR", NULL};
    for (int i = 0; env_names[i]; i++) {
        const char *value = g_getenv(env_names[i]);
        if (value && value[0]) g_ptr_array_add(args, g_strdup_printf("%s=%s", env_names[i], value));
    }
    const char *session_bus = g_getenv("DBUS_SESSION_BUS_ADDRESS");
    if (session_bus && session_bus[0]) g_ptr_array_add(args, g_strdup_printf("DBUS_SESSION_BUS_ADDRESS=%s", session_bus));
    const char *home = g_get_home_dir();
    if (home && home[0]) g_ptr_array_add(args, g_strdup_printf("DEEPCOOL_ORIGINAL_HOME=%s", home));
    g_ptr_array_add(args, g_strdup_printf("DEEPCOOL_ORIGINAL_UID=%lu", (unsigned long)getuid()));

    g_ptr_array_add(args, g_strdup(exe));
    for (int i = 1; i < argc; i++) g_ptr_array_add(args, g_strdup(argv[i]));
    g_ptr_array_add(args, NULL);

    int status = 1;
    GError *error = NULL;
    gboolean ok = g_spawn_sync(NULL, (char **)args->pdata, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &status, &error);
    if (!ok) {
        fprintf(stderr, "Failed to request administrator permission: %s\n", error ? error->message : "unknown error");
        g_clear_error(&error);
        status = 1;
    }
    g_ptr_array_free(args, TRUE);
    return status == 0 ? 0 : 1;
}

static int install_to_local(void) {
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) {
        fprintf(stderr, "Failed to resolve executable path.\n");
        return 1;
    }
    exe[n] = 0;

    char error[512] = "";
    if (!desktop_integration_install(exe, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error[0] ? error : "Failed to install DeepCool Digital Linux.");
        return 1;
    }

    printf("Installed DeepCool Digital Linux to ~/.local/bin/deepcool-digital-linux\n");
    printf("Desktop entry installed to ~/.local/share/applications/unnoficial.deepcool.digital.linux.desktop\n");
    printf("If it does not appear immediately, log out and back in or refresh your application menu.\n");
    return 0;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--install") == 0) return install_to_local();
        if (strcmp(argv[i], "--background") == 0) return background_run();
    }

    if (geteuid() != 0) return relaunch_with_pkexec(argc, argv);

    AppState state;
    memset(&state, 0, sizeof(state));
    g_mutex_init(&state.lock);

    GtkApplication *app = gtk_application_new("unnoficial.deepcool.digital.linux", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(ui_activate), &state);
    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_mutex_lock(&state.lock);
    state.stop_requested = true;
    g_mutex_unlock(&state.lock);
    if (state.worker) g_thread_join(state.worker);

    g_object_unref(app);
    hid_exit();
    return status;
}
