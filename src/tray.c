#include "tray.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define APP_ID "unnoficial.deepcool.digital.linux"
#define ICON_NAME "deepcool-digital-linux"
#define RESOURCE_ICON "/io/github/deepcool/digital/linux/deepcool.png"
#define SNI_PATH "/StatusNotifierItem"

struct TrayIcon {
    GDBusConnection *connection;
    GDBusNodeInfo *node;
    guint registration_id;
    guint name_id;
    GMainContext *context;
    GMainLoop *loop;
    GThread *thread;
};

static const char *sni_xml =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <method name='Activate'><arg type='i' name='x' direction='in'/><arg type='i' name='y' direction='in'/></method>"
    "    <method name='ContextMenu'><arg type='i' name='x' direction='in'/><arg type='i' name='y' direction='in'/></method>"
    "    <method name='SecondaryActivate'><arg type='i' name='x' direction='in'/><arg type='i' name='y' direction='in'/></method>"
    "    <method name='Scroll'><arg type='i' name='delta' direction='in'/><arg type='s' name='orientation' direction='in'/></method>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='WindowId' type='u' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "    <property name='IconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <signal name='NewIcon'/>"
    "    <signal name='NewStatus'><arg type='s' name='status'/></signal>"
    "    <signal name='NewToolTip'/>"
    "  </interface>"
    "</node>";

static GVariant *empty_pixmaps(void) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(iiay)"));
    return g_variant_builder_end(&builder);
}

static GVariant *get_sni_property(GDBusConnection *connection, const char *sender, const char *object_path,
                                  const char *interface_name, const char *property_name, GError **error,
                                  gpointer user_data) {
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)error;
    (void)user_data;

    if (g_strcmp0(property_name, "Category") == 0) return g_variant_new_string("ApplicationStatus");
    if (g_strcmp0(property_name, "Id") == 0) return g_variant_new_string(ICON_NAME);
    if (g_strcmp0(property_name, "Title") == 0) return g_variant_new_string("DeepCool Digital");
    if (g_strcmp0(property_name, "Status") == 0) return g_variant_new_string("Active");
    if (g_strcmp0(property_name, "WindowId") == 0) return g_variant_new_uint32(0);
    if (g_strcmp0(property_name, "IconName") == 0) return g_variant_new_string(ICON_NAME);
    if (g_strcmp0(property_name, "IconPixmap") == 0) return empty_pixmaps();
    if (g_strcmp0(property_name, "ToolTip") == 0) return g_variant_new("(s@a(iiay)ss)", ICON_NAME, empty_pixmaps(), "DeepCool Digital", "Running in background");
    if (g_strcmp0(property_name, "ItemIsMenu") == 0) return g_variant_new_boolean(FALSE);
    return NULL;
}

static void handle_sni_method(GDBusConnection *connection, const char *sender, const char *object_path,
                              const char *interface_name, const char *method_name, GVariant *parameters,
                              GDBusMethodInvocation *invocation, gpointer user_data) {
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)method_name;
    (void)parameters;
    (void)user_data;
    g_dbus_method_invocation_return_value(invocation, NULL);
}

static const GDBusInterfaceVTable sni_vtable = {
    .method_call = handle_sni_method,
    .get_property = get_sni_property,
};

static gpointer tray_thread_run(gpointer user_data) {
    TrayIcon *tray = user_data;
    GError *error = NULL;
    g_main_context_push_thread_default(tray->context);
    tray->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!tray->connection) {
        g_clear_error(&error);
        g_main_context_pop_thread_default(tray->context);
        return NULL;
    }
    tray->node = g_dbus_node_info_new_for_xml(sni_xml, &error);
    if (!tray->node) {
        g_clear_error(&error);
        g_main_context_pop_thread_default(tray->context);
        return NULL;
    }
    tray->registration_id = g_dbus_connection_register_object(
        tray->connection, SNI_PATH, tray->node->interfaces[0], &sni_vtable, tray, NULL, &error);
    if (!tray->registration_id) {
        g_clear_error(&error);
        g_main_context_pop_thread_default(tray->context);
        return NULL;
    }

    char *bus_name = g_strdup_printf("org.kde.StatusNotifierItem-%d-1", getpid());
    tray->name_id = g_bus_own_name_on_connection(tray->connection, bus_name, G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);
    g_dbus_connection_call_sync(
        tray->connection,
        "org.kde.StatusNotifierWatcher",
        "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher",
        "RegisterStatusNotifierItem",
        g_variant_new("(s)", bus_name),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        2000,
        NULL,
        NULL);
    g_free(bus_name);

    tray->loop = g_main_loop_new(tray->context, FALSE);
    g_main_loop_run(tray->loop);
    g_main_loop_unref(tray->loop);
    tray->loop = NULL;
    g_main_context_pop_thread_default(tray->context);
    return NULL;
}

TrayIcon *tray_icon_start(void) {
    TrayIcon *tray = g_new0(TrayIcon, 1);
    tray->context = g_main_context_new();
    tray->thread = g_thread_new("deepcool-tray", tray_thread_run, tray);
    return tray;
}

void tray_icon_stop(TrayIcon *tray) {
    if (!tray) return;
    if (tray->loop) g_main_loop_quit(tray->loop);
    if (tray->thread) g_thread_join(tray->thread);
    if (tray->name_id) g_bus_unown_name(tray->name_id);
    if (tray->registration_id && tray->connection) g_dbus_connection_unregister_object(tray->connection, tray->registration_id);
    if (tray->node) g_dbus_node_info_unref(tray->node);
    if (tray->connection) g_object_unref(tray->connection);
    if (tray->context) g_main_context_unref(tray->context);
    g_free(tray);
}

static char *original_home(void) {
    const char *home = g_getenv("DEEPCOOL_ORIGINAL_HOME");
    if (home && home[0]) return g_strdup(home);
    const char *uid_text = g_getenv("PKEXEC_UID");
    if (uid_text && uid_text[0]) {
        struct passwd *pw = getpwuid((uid_t)g_ascii_strtoull(uid_text, NULL, 10));
        if (pw && pw->pw_dir) return g_strdup(pw->pw_dir);
    }
    return g_strdup(g_get_home_dir());
}

static uid_t original_uid(void) {
    const char *uid_text = g_getenv("DEEPCOOL_ORIGINAL_UID");
    if (!uid_text || !uid_text[0]) uid_text = g_getenv("PKEXEC_UID");
    if (uid_text && uid_text[0]) return (uid_t)g_ascii_strtoull(uid_text, NULL, 10);
    return getuid();
}

static void chown_if_needed(const char *path) {
    uid_t uid = original_uid();
    if (uid != 0 && chown(path, uid, uid) != 0) {
        g_debug("Failed to chown %s", path);
    }
}

static char *desktop_quote(const char *value) {
    GString *quoted = g_string_new("\"");
    for (const char *p = value; *p; p++) {
        if (*p == '"' || *p == '\\') g_string_append_c(quoted, '\\');
        g_string_append_c(quoted, *p);
    }
    g_string_append_c(quoted, '"');
    return g_string_free(quoted, FALSE);
}

bool desktop_integration_install(const char *source_exe_path, char *error, size_t error_len) {
    char *home = original_home();
    char *bin_dir = g_build_filename(home, ".local", "bin", NULL);
    char *apps_dir = g_build_filename(home, ".local", "share", "applications", NULL);
    char *icons_dir = g_build_filename(home, ".local", "share", "icons", "hicolor", "256x256", "apps", NULL);
    if (g_mkdir_with_parents(bin_dir, 0755) != 0 || g_mkdir_with_parents(apps_dir, 0755) != 0 || g_mkdir_with_parents(icons_dir, 0755) != 0) {
        snprintf(error, error_len, "Failed to create ~/.local installation directories");
        g_free(bin_dir);
        g_free(apps_dir);
        g_free(icons_dir);
        g_free(home);
        return false;
    }
    chown_if_needed(bin_dir);
    chown_if_needed(apps_dir);
    chown_if_needed(icons_dir);

    char *bin_path = g_build_filename(bin_dir, "deepcool-digital-linux", NULL);
    char *desktop_path = g_build_filename(apps_dir, APP_ID ".desktop", NULL);
    char *icon_path = g_build_filename(icons_dir, ICON_NAME ".png", NULL);

    GFile *source = g_file_new_for_path(source_exe_path);
    GFile *dest = g_file_new_for_path(bin_path);
    GError *copy_error = NULL;
    bool ok = g_file_copy(source, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &copy_error);
    g_object_unref(source);
    g_object_unref(dest);
    if (!ok) {
        snprintf(error, error_len, "Failed to copy binary to %s: %s", bin_path, copy_error ? copy_error->message : "unknown error");
        g_clear_error(&copy_error);
        g_free(bin_path);
        g_free(desktop_path);
        g_free(icon_path);
        g_free(bin_dir);
        g_free(apps_dir);
        g_free(icons_dir);
        g_free(home);
        return false;
    }
    chmod(bin_path, 0755);
    chown_if_needed(bin_path);

    GError *resource_error = NULL;
    GBytes *bytes = g_resources_lookup_data(RESOURCE_ICON, G_RESOURCE_LOOKUP_FLAGS_NONE, &resource_error);
    ok = bytes && g_file_set_contents(icon_path, g_bytes_get_data(bytes, NULL), (gssize)g_bytes_get_size(bytes), NULL);
    if (bytes) g_bytes_unref(bytes);
    g_clear_error(&resource_error);
    if (!ok) snprintf(error, error_len, "Failed to install application icon");

    char *exe_quoted = desktop_quote(bin_path);
    char *desktop = g_strdup_printf(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=DeepCool Digital Linux\n"
        "Comment=DeepCool Digital device monitor\n"
        "Exec=%s\n"
        "Icon=%s\n"
        "Terminal=false\n"
        "Categories=Utility;HardwareSettings;\n"
        "StartupNotify=true\n"
        "StartupWMClass=%s\n",
        exe_quoted,
        ICON_NAME,
        APP_ID);
    if (!g_file_set_contents(desktop_path, desktop, -1, NULL)) {
        snprintf(error, error_len, "Failed to write %s", desktop_path);
        ok = false;
    }
    chown_if_needed(desktop_path);
    chown_if_needed(icon_path);
    g_free(desktop);
    g_free(exe_quoted);
    g_free(bin_path);
    g_free(desktop_path);
    g_free(icon_path);
    g_free(bin_dir);
    g_free(apps_dir);
    g_free(icons_dir);
    g_free(home);
    return ok;
}
