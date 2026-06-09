#include "ui.h"

#include "../app_id.h"
#include "../devices/protocol.h"
#include "../service.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>

static void capture_config(AppState *state);
static void schedule_config_restart(AppState *state);

static void set_status(AppState *state, const char *status) {
    g_mutex_lock(&state->lock);
    snprintf(state->status, sizeof(state->status), "%s", status);
    g_mutex_unlock(&state->lock);
}

static void install_deepcool_css(void) {
    const char *css =
        "window { background: #ffffff; color: #20252b; }"
        ".app-root { background: #ffffff; }"
        ".topbar { background: #ffffff; border-bottom: 1px solid #eceff1; padding: 10px 16px; }"
        ".logo-text { color: #11171d; font-weight: 900; font-size: 18px; letter-spacing: 0; }"
        ".sidebar { background: #fbfbfb; border-right: 1px solid #eceff1; padding: 14px 10px; }"
        ".nav-icon { background: transparent; border: 0; box-shadow: none; color: #8a949b; font-size: 22px; padding: 12px 8px; }"
        ".nav-icon:hover { background: #f1f4f5; color: #009b9b; }"
        ".nav-active { color: #009b9b; border-left: 3px solid #009b9b; }"
        ".side-panel { background: #ffffff; border: 0; padding: 16px; }"
        ".main-panel { background: #ffffff; padding: 16px; }"
        ".card { background: #fbfbfb; border: 1px solid #edf0f2; border-radius: 4px; padding: 16px; }"
        ".device-card { background: #f8f8f8; border: 1px solid #f0f0f0; border-radius: 2px; padding: 18px; }"
        ".brand { color: #20252b; font-weight: 800; font-size: 22px; letter-spacing: 0; }"
        ".section-title { color: #20252b; font-weight: 800; font-size: 16px; }"
        ".status { color: #009b9b; font-weight: 600; }"
        ".caption { color: #879097; font-weight: 600; }"
        ".metric-row { background: transparent; border: 0; padding: 2px 0; }"
        ".metric-title { color: #879097; }"
        ".metric-value { color: #008f8f; font-weight: 800; }"
        ".service-row { padding: 2px 0; }"
        ".service-switch { margin: 0; }"
        ".big-load { color: #20252b; font-weight: 900; font-size: 38px; }"
        ".primary-action { background: #009b9b; color: #ffffff; font-weight: 700; border-radius: 2px; }"
        ".secondary-action { background: #f6f6f6; color: #343a40; border-radius: 2px; }"
        "dropdown, entry { background: #ffffff; color: #343a40; border: 1px solid #d6dadd; border-radius: 3px; }"
        "scale trough { background: #e8ecef; }"
        "scale highlight { background: #109c9b; }"
        "switch:checked { background: #109c9b; }";
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void install_app_icon(void) {
    GdkDisplay *display = gdk_display_get_default();
    if (!display) return;
    GtkIconTheme *theme = gtk_icon_theme_get_for_display(display);
    gtk_icon_theme_add_resource_path(theme, "/io/github/deepcool/digital/linux");
    gtk_window_set_default_icon_name(DEEPCOOL_ICON_NAME);
}

static void draw_gauge(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data, bool gpu) {
    (void)area;
    AppState *state = user_data;
    g_mutex_lock(&state->lock);
    double value = gpu ? state->gpu_usage : state->cpu_usage;
    g_mutex_unlock(&state->lock);

    double cx = width * 0.5;
    double cy = height * 0.78;
    double radius = MIN(width * 0.38, height * 0.68);
    double start = M_PI * 1.12;
    double end = M_PI * 1.88;
    double pct = CLAMP(value / 100.0, 0.0, 1.0);

    cairo_set_line_width(cr, 7.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_source_rgb(cr, 0.92, 0.93, 0.94);
    cairo_arc(cr, cx, cy, radius, start, end);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.05, 0.62, 0.62);
    cairo_arc(cr, cx, cy, radius, start, start + (end - start) * pct);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_source_rgb(cr, 0.55, 0.58, 0.60);
    cairo_set_font_size(cr, 12);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, "Load", &ext);
    cairo_move_to(cr, cx - ext.width / 2, cy - radius * 0.35);
    cairo_show_text(cr, "Load");

    char text[16];
    snprintf(text, sizeof(text), "%.0f", value);
    cairo_set_source_rgb(cr, 0.12, 0.14, 0.16);
    cairo_set_font_size(cr, 42);
    cairo_text_extents(cr, text, &ext);
    cairo_move_to(cr, cx - ext.width / 2, cy - radius * 0.02);
    cairo_show_text(cr, text);
    cairo_set_font_size(cr, 14);
    cairo_move_to(cr, cx + ext.width / 2 + 3, cy - radius * 0.02);
    cairo_show_text(cr, "%");

    cairo_set_source_rgb(cr, 0.52, 0.55, 0.57);
    cairo_set_font_size(cr, 11);
    cairo_move_to(cr, cx - radius - 8, cy - 4);
    cairo_show_text(cr, "0");
    cairo_move_to(cr, cx + radius + 4, cy - 4);
    cairo_show_text(cr, "120");
}

static void draw_cpu_gauge(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    draw_gauge(area, cr, width, height, user_data, false);
}

static void draw_gpu_gauge(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    draw_gauge(area, cr, width, height, user_data, true);
}

static void draw_fan(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)area;
    AppState *state = user_data;
    g_mutex_lock(&state->lock);
    double phase = state->fan_phase;
    double rpm = state->cpu_fan_rpm;
    g_mutex_unlock(&state->lock);

    double cx = width / 2.0;
    double cy = height / 2.0;
    double r = MIN(width, height) * 0.35;
    cairo_set_source_rgb(cr, 0.96, 0.97, 0.97);
    cairo_arc(cr, cx, cy, r * 1.35, 0, M_PI * 2);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.86, 0.88, 0.89);
    cairo_set_line_width(cr, 2);
    cairo_arc(cr, cx, cy, r * 1.35, 0, M_PI * 2);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, rpm > 0 ? 0.05 : 0.75, rpm > 0 ? 0.62 : 0.75, rpm > 0 ? 0.62 : 0.75);
    for (int i = 0; i < 4; i++) {
        double a = phase + i * M_PI / 2.0;
        cairo_save(cr);
        cairo_translate(cr, cx, cy);
        cairo_rotate(cr, a);
        cairo_move_to(cr, 0, -4);
        cairo_curve_to(cr, r * 0.75, -r * 0.42, r * 1.02, -r * 0.05, r * 0.25, r * 0.12);
        cairo_curve_to(cr, r * 0.04, r * 0.16, -r * 0.08, r * 0.08, 0, -4);
        cairo_fill(cr);
        cairo_restore(cr);
    }
    cairo_set_source_rgb(cr, 0.12, 0.14, 0.16);
    cairo_arc(cr, cx, cy, r * 0.14, 0, M_PI * 2);
    cairo_fill(cr);
}

static gboolean ui_refresh(gpointer user_data) {
    AppState *state = user_data;
    char buf[256];
    g_mutex_lock(&state->lock);
    gtk_label_set_text(GTK_LABEL(state->status_label), state->status);
    snprintf(buf, sizeof(buf), "%.0f", state->cpu_temp); gtk_label_set_text(GTK_LABEL(state->cpu_temp_label), buf);
    snprintf(buf, sizeof(buf), "%.0f%%", state->cpu_usage); gtk_label_set_text(GTK_LABEL(state->cpu_usage_label), buf);
    snprintf(buf, sizeof(buf), "%.0f W", state->cpu_power); gtk_label_set_text(GTK_LABEL(state->cpu_power_label), buf);
    snprintf(buf, sizeof(buf), "%.0f MHz", state->cpu_freq); gtk_label_set_text(GTK_LABEL(state->cpu_freq_label), buf);
    if (state->cpu.fan_sensor[0]) snprintf(buf, sizeof(buf), "%.0f RPM", state->cpu_fan_rpm);
    else snprintf(buf, sizeof(buf), "N/A");
    gtk_label_set_text(GTK_LABEL(state->cpu_fan_label), buf);
    snprintf(buf, sizeof(buf), "%.0f", state->gpu_temp); gtk_label_set_text(GTK_LABEL(state->gpu_temp_label), buf);
    if (state->gpu.vendor != GPU_NONE) snprintf(buf, sizeof(buf), "%.0f%%", state->gpu_usage);
    else snprintf(buf, sizeof(buf), "N/A");
    gtk_label_set_text(GTK_LABEL(state->gpu_usage_label), buf);
    snprintf(buf, sizeof(buf), "%.0f W", state->gpu_power); gtk_label_set_text(GTK_LABEL(state->gpu_power_label), buf);
    snprintf(buf, sizeof(buf), "%.0f MHz", state->gpu_freq); gtk_label_set_text(GTK_LABEL(state->gpu_freq_label), buf);
    state->fan_phase += 0.08 + MIN(state->cpu_fan_rpm / 3000.0, 1.5) * 0.32;
    if (state->fan_phase > M_PI * 2) state->fan_phase -= M_PI * 2;
    g_mutex_unlock(&state->lock);
    gtk_widget_queue_draw(state->cpu_gauge);
    gtk_widget_queue_draw(state->gpu_gauge);
    gtk_widget_queue_draw(state->fan_area);
    return G_SOURCE_CONTINUE;
}

static void stop_worker(AppState *state) {
    g_mutex_lock(&state->lock);
    bool has_worker = state->worker != NULL;
    state->stop_requested = true;
    g_mutex_unlock(&state->lock);
    if (has_worker) {
        g_thread_join(state->worker);
        state->worker = NULL;
    }
}

static bool start_worker(AppState *state) {
    if (state->needs_root_warning) {
        set_status(state, "Administrator privileges required. Restart with sudo to control the device.");
        return false;
    }
    if (state->device_count == 0) {
        set_status(state, "No DeepCool device found");
        return false;
    }
    capture_config(state);
    DeepCoolDevice *dev = &state->devices[state->config.device_index];
    if (!mode_supported(dev->series, state->config.mode, false) || !mode_supported(dev->series, state->config.secondary, true)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Mode is not supported by %s.", series_name(dev->series));
        set_status(state, msg);
        return false;
    }
    char error[256] = "";
    app_config_save(state, error, sizeof(error));

    g_mutex_lock(&state->lock);
    state->running = true;
    state->stop_requested = false;
    snprintf(state->status, sizeof(state->status), "Applying settings...");
    g_mutex_unlock(&state->lock);
    state->worker = g_thread_new("deepcool-worker", device_worker_main, state);
    return true;
}

static gboolean restart_worker_cb(gpointer user_data) {
    AppState *state = user_data;
    state->restart_source_id = 0;
    stop_worker(state);
    start_worker(state);
    return G_SOURCE_REMOVE;
}

static void schedule_config_restart(AppState *state) {
    if (state->suppress_config_events) return;
    if (state->restart_source_id) g_source_remove(state->restart_source_id);
    state->restart_source_id = g_timeout_add(350, restart_worker_cb, state);
}

static void update_service_status(AppState *state) {
    if (!state->service_status_label) return;
    bool enabled = service_is_enabled();
    bool active = service_is_active();
    state->suppress_service_events = true;
    gtk_switch_set_active(GTK_SWITCH(state->service_switch), enabled);
    state->suppress_service_events = false;
    gtk_label_set_text(GTK_LABEL(state->service_status_label),
                       active ? "Background service is running" :
                       enabled ? "Background service is enabled" :
                       "Background service is disabled");
}

typedef struct {
    AppState *state;
    bool enable;
    bool ok;
    char error[512];
} ServiceTask;

static gboolean service_task_finish(gpointer user_data) {
    ServiceTask *task = user_data;
    if (!task->ok) {
        gtk_label_set_text(GTK_LABEL(task->state->service_status_label), task->error[0] ? task->error : "Failed to update background service");
        task->state->suppress_service_events = true;
        gtk_switch_set_active(GTK_SWITCH(task->state->service_switch), !task->enable);
        task->state->suppress_service_events = false;
    } else {
        update_service_status(task->state);
    }
    gtk_widget_set_sensitive(task->state->service_switch, TRUE);
    g_free(task);
    return G_SOURCE_REMOVE;
}

static gpointer service_task_run(gpointer user_data) {
    ServiceTask *task = user_data;
    task->ok = task->enable ? service_install_enable(task->state, task->error, sizeof(task->error)) : service_disable(task->error, sizeof(task->error));
    g_idle_add(service_task_finish, task);
    return NULL;
}

static gboolean service_switch_changed(GtkSwitch *sw, gboolean state_value, gpointer user_data) {
    AppState *state = user_data;
    if (state->suppress_service_events) return FALSE;
    capture_config(state);
    gtk_widget_set_sensitive(GTK_WIDGET(sw), FALSE);
    gtk_label_set_text(GTK_LABEL(state->service_status_label), state_value ? "Requesting administrator permission..." : "Disabling background service...");
    ServiceTask *task = g_new0(ServiceTask, 1);
    task->state = state;
    task->enable = state_value;
    g_thread_unref(g_thread_new("deepcool-service", service_task_run, task));
    return TRUE;
}

static GtkWidget *labeled(GtkWidget *child, const char *label) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *lbl = gtk_label_new(label);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(lbl, "caption");
    gtk_box_append(GTK_BOX(box), lbl);
    gtk_box_append(GTK_BOX(box), child);
    return box;
}

static GtkStringList *string_list_from_modes(const char *const *items) {
    GtkStringList *list = gtk_string_list_new(NULL);
    for (int i = 0; items[i]; i++) gtk_string_list_append(list, items[i]);
    return list;
}

static GtkStringList *string_list_from_presets(const DisplayPreset *items) {
    GtkStringList *list = gtk_string_list_new(NULL);
    for (int i = 0; items[i].label; i++) gtk_string_list_append(list, items[i].label);
    return list;
}

static GtkWidget *drop_down_from_list(GtkStringList *list, guint active) {
    GtkWidget *dd = gtk_drop_down_new(G_LIST_MODEL(list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), active);
    return dd;
}

static int dropdown_selected(GtkWidget *dd) {
    return (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(dd));
}

static const char *dropdown_string(GtkWidget *dd) {
    GtkStringObject *obj = GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(dd)));
    return obj ? gtk_string_object_get_string(obj) : "";
}

static DeviceSeries selected_series(AppState *state) {
    int index = dropdown_selected(state->device_combo);
    if (index < 0 || (size_t)index >= state->device_count) return SERIES_UNKNOWN;
    return state->devices[index].series;
}

static void set_drop_down_presets(GtkWidget *dd, const DisplayPreset *items) {
    gtk_drop_down_set_model(GTK_DROP_DOWN(dd), G_LIST_MODEL(string_list_from_presets(items)));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), 0);
}

static void select_drop_down_string(GtkWidget *dd, const char *value) {
    GListModel *model = gtk_drop_down_get_model(GTK_DROP_DOWN(dd));
    guint n = g_list_model_get_n_items(model);
    for (guint i = 0; i < n; i++) {
        GtkStringObject *obj = g_list_model_get_item(model, i);
        bool match = g_strcmp0(gtk_string_object_get_string(obj), value) == 0;
        g_object_unref(obj);
        if (match) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), i);
            return;
        }
    }
}

static void select_display_preset(AppState *state) {
    DeviceSeries series = selected_series(state);
    const DisplayPreset *presets = display_presets_for_series(series);
    Mode primary = state->config.mode == MODE_DEFAULT ? effective_mode(series, MODE_DEFAULT) : state->config.mode;
    Mode secondary = state->config.secondary;

    for (int i = 0; presets[i].label; i++) {
        if (presets[i].primary == primary && presets[i].secondary == secondary) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(state->mode_combo), (guint)i);
            return;
        }
    }

    for (int i = 0; presets[i].label; i++) {
        if (presets[i].primary == primary) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(state->mode_combo), (guint)i);
            return;
        }
    }

    gtk_drop_down_set_selected(GTK_DROP_DOWN(state->mode_combo), 0);
}

static bool selected_display_cycles(AppState *state) {
    DeviceSeries series = selected_series(state);
    const DisplayPreset *presets = display_presets_for_series(series);
    int preset_index = dropdown_selected(state->mode_combo);
    return preset_index >= 0 && presets[preset_index].label && presets[preset_index].primary == MODE_AUTO;
}

static void capability_summary(DeviceSeries series, char *out, size_t out_len) {
    bool fahrenheit = series != SERIES_AG && series != SERIES_UNKNOWN;
    bool alarm = series == SERIES_AG || series == SERIES_AK || series == SERIES_LS;
    bool zeros = series == SERIES_LD;
    bool gpu = series == SERIES_LP || series == SERIES_CH || series == SERIES_CH_GEN2 || series == SERIES_CH510;
    snprintf(out, out_len, "Fahrenheit %s  ·  Alarm %s  ·  LD zeros %s  ·  GPU data %s",
             fahrenheit ? "yes" : "no",
             alarm ? "yes" : "no",
             zeros ? "yes" : "no",
             gpu ? "yes" : "no");
}

static void update_device_controls(AppState *state) {
    bool previous_suppress = state->suppress_config_events;
    state->suppress_config_events = true;
    DeviceSeries series = selected_series(state);
    set_drop_down_presets(state->mode_combo, display_presets_for_series(series));
    select_display_preset(state);

    bool gpu_supported = series == SERIES_LP || series == SERIES_CH || series == SERIES_CH_GEN2 || series == SERIES_CH510;
    bool fahrenheit_supported = series != SERIES_AG && series != SERIES_UNKNOWN;
    bool alarm_supported = series == SERIES_AG || series == SERIES_AK || series == SERIES_LS;
    bool zeros_supported = series == SERIES_LD;
    gtk_widget_set_sensitive(state->gpu_combo, gpu_supported);
    gtk_widget_set_sensitive(state->fahrenheit_switch, fahrenheit_supported);
    gtk_widget_set_sensitive(state->alarm_switch, alarm_supported);
    gtk_widget_set_sensitive(state->rotate_combo, series == SERIES_LP);
    gtk_widget_set_sensitive(state->zeros_switch, zeros_supported);
    gtk_widget_set_sensitive(state->auto_interval_scale, selected_display_cycles(state));
    if (state->fahrenheit_label) gtk_label_set_text(GTK_LABEL(state->fahrenheit_label),
        fahrenheit_supported ? "Fahrenheit - supported by this device" : "Fahrenheit - not supported by this device");
    if (state->alarm_label) gtk_label_set_text(GTK_LABEL(state->alarm_label),
        alarm_supported ? "High temperature alarm - supported by this device" : "High temperature alarm - not supported by this device");
    if (state->zeros_label) gtk_label_set_text(GTK_LABEL(state->zeros_label),
        zeros_supported ? "LD leading zeros - supported by this device" : "LD leading zeros - LD Series only");

    int index = dropdown_selected(state->device_combo);
    if (state->device_name_label && index >= 0 && (size_t)index < state->device_count) {
        gtk_label_set_text(GTK_LABEL(state->device_name_label), state->devices[index].name);
        gtk_label_set_text(GTK_LABEL(state->device_kind_label), series_name(state->devices[index].series));
    }
    state->suppress_config_events = previous_suppress;
}

static void device_changed(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)object;
    (void)pspec;
    AppState *state = user_data;
    update_device_controls(state);
    schedule_config_restart(state);
}

static void config_changed(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)object;
    (void)pspec;
    AppState *state = user_data;
    gtk_widget_set_sensitive(state->auto_interval_scale, selected_display_cycles(state));
    schedule_config_restart(state);
}

static void config_value_changed(GtkRange *range, gpointer user_data) {
    (void)range;
    schedule_config_restart(user_data);
}

static void capture_config(AppState *state) {
    state->config.device_index = dropdown_selected(state->device_combo);
    state->config.gpu_index = dropdown_selected(state->gpu_combo) - 1;
    DeviceSeries series = selected_series(state);
    const DisplayPreset *presets = display_presets_for_series(series);
    int preset_index = dropdown_selected(state->mode_combo);
    if (preset_index >= 0 && presets[preset_index].label) {
        state->config.mode = presets[preset_index].primary;
        state->config.secondary = presets[preset_index].secondary;
    } else {
        state->config.mode = effective_mode(series, MODE_DEFAULT);
        state->config.secondary = MODE_DEFAULT;
    }
    state->config.fahrenheit = gtk_switch_get_active(GTK_SWITCH(state->fahrenheit_switch));
    state->config.alarm = gtk_switch_get_active(GTK_SWITCH(state->alarm_switch));
    state->config.lead_zeros = gtk_switch_get_active(GTK_SWITCH(state->zeros_switch));
    state->config.update_ms = (uint32_t)gtk_range_get_value(GTK_RANGE(state->update_scale));
    state->config.auto_interval_ms = (uint32_t)gtk_range_get_value(GTK_RANGE(state->auto_interval_scale));
    state->config.rotate = (uint16_t)atoi(dropdown_string(state->rotate_combo));
    if (state->config.mode == MODE_DEFAULT && state->device_count > 0 && state->config.device_index >= 0 && (size_t)state->config.device_index < state->device_count) {
        DeepCoolDevice *dev = &state->devices[state->config.device_index];
        state->config.mode = effective_mode(dev->series, MODE_DEFAULT);
    }
}

static void nav_clicked(GtkButton *button, gpointer user_data) {
    AppState *state = user_data;
    const char *page = g_object_get_data(G_OBJECT(button), "page");
    if (!page) return;
    gtk_stack_set_visible_child_name(GTK_STACK(state->content_stack), page);
    gtk_widget_remove_css_class(state->nav_monitor_button, "nav-active");
    gtk_widget_remove_css_class(state->nav_device_button, "nav-active");
    gtk_widget_remove_css_class(state->nav_settings_button, "nav-active");
    if (g_strcmp0(page, "monitor") == 0) gtk_widget_add_css_class(state->nav_monitor_button, "nav-active");
    else if (g_strcmp0(page, "device") == 0) gtk_widget_add_css_class(state->nav_device_button, "nav-active");
    else gtk_widget_add_css_class(state->nav_settings_button, "nav-active");
}

static GtkWidget *nav_button(const char *icon, const char *page, AppState *state, bool active) {
    GtkWidget *button = gtk_button_new_with_label(icon);
    gtk_widget_add_css_class(button, "nav-icon");
    if (active) gtk_widget_add_css_class(button, "nav-active");
    g_object_set_data_full(G_OBJECT(button), "page", g_strdup(page), g_free);
    g_signal_connect(button, "clicked", G_CALLBACK(nav_clicked), state);
    return button;
}

static GtkWidget *metric_stack(const char *title, GtkWidget **value) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    *value = gtk_label_new("-");
    gtk_widget_add_css_class(*value, "metric-value");
    gtk_widget_set_halign(*value, GTK_ALIGN_START);
    GtkWidget *caption = gtk_label_new(title);
    gtk_widget_add_css_class(caption, "metric-title");
    gtk_widget_set_halign(caption, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), *value);
    gtk_box_append(GTK_BOX(box), caption);
    return box;
}

static GtkWidget *system_card(const char *title, const char *device_name, GtkWidget *gauge, GtkWidget *metrics) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_set_hexpand(card, TRUE);

    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(left, TRUE);
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *title_label = gtk_label_new(title);
    gtk_widget_add_css_class(title_label, "section-title");
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    GtkWidget *device_label = gtk_label_new(device_name);
    gtk_widget_add_css_class(device_label, "metric-title");
    gtk_widget_set_halign(device_label, GTK_ALIGN_END);
    gtk_widget_set_hexpand(device_label, TRUE);
    gtk_box_append(GTK_BOX(head), title_label);
    gtk_box_append(GTK_BOX(head), device_label);
    gtk_box_append(GTK_BOX(left), head);
    gtk_box_append(GTK_BOX(left), gauge);

    gtk_box_append(GTK_BOX(card), left);
    gtk_box_append(GTK_BOX(card), metrics);
    return card;
}

void ui_activate(GtkApplication *app, gpointer user_data) {
    AppState *state = user_data;
    install_deepcool_css();
    install_app_icon();
    state->app = app;
    state->device_count = scan_devices(state->devices, MAX_DEVICES);
    state->gpu_count = scan_gpus(state->gpus, MAX_GPUS);
    state->config.update_ms = 1000;
    state->config.auto_interval_ms = AUTO_MODE_INTERVAL_MS;
    state->config.mode = MODE_DEFAULT;
    state->config.secondary = MODE_DEFAULT;
    state->config.gpu_index = state->gpu_count > 0 ? 0 : -1;
    app_config_load(state);
    snprintf(state->status, sizeof(state->status), "%s",
             state->needs_root_warning ? "Administrator privileges required. Restart with sudo." : "Ready");

    GtkWidget *win = gtk_application_window_new(app);
    state->window = win;
    gtk_window_set_title(GTK_WINDOW(win), "DeepCool Digital Linux");
    gtk_window_set_icon_name(GTK_WINDOW(win), DEEPCOOL_ICON_NAME);
    gtk_window_set_default_size(GTK_WINDOW(win), 1180, 720);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root, "app-root");
    gtk_window_set_child(GTK_WINDOW(win), root);

    GtkWidget *topbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(topbar, "topbar");
    GtkWidget *logo = gtk_picture_new_for_resource(DEEPCOOL_RESOURCE_ICON);
    gtk_picture_set_content_fit(GTK_PICTURE(logo), GTK_CONTENT_FIT_CONTAIN);
    gtk_picture_set_can_shrink(GTK_PICTURE(logo), TRUE);
    gtk_widget_set_size_request(logo, 18, 18);
    gtk_widget_set_halign(logo, GTK_ALIGN_START);
    gtk_widget_set_valign(logo, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(topbar), logo);
    GtkWidget *logo_text = gtk_label_new("DEEPCOOL");
    gtk_widget_add_css_class(logo_text, "logo-text");
    gtk_box_append(GTK_BOX(topbar), logo_text);
    GtkWidget *top_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(top_spacer, TRUE);
    gtk_box_append(GTK_BOX(topbar), top_spacer);
    gtk_box_append(GTK_BOX(root), topbar);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(body, TRUE);
    gtk_box_append(GTK_BOX(root), body);

    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(sidebar, "sidebar");
    gtk_widget_set_size_request(sidebar, 56, -1);
    state->nav_monitor_button = nav_button("◴", "monitor", state, true);
    state->nav_device_button = nav_button("▣", "device", state, false);
    gtk_box_append(GTK_BOX(sidebar), state->nav_monitor_button);
    gtk_box_append(GTK_BOX(sidebar), state->nav_device_button);
    GtkWidget *nav_spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(nav_spacer, TRUE);
    gtk_box_append(GTK_BOX(sidebar), nav_spacer);
    state->nav_settings_button = nav_button("⚙", "settings", state, false);
    gtk_box_append(GTK_BOX(sidebar), state->nav_settings_button);
    gtk_box_append(GTK_BOX(body), sidebar);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(controls, "side-panel");
    gtk_widget_set_size_request(controls, 300, -1);
    gtk_box_append(GTK_BOX(body), controls);

    GtkWidget *controls_title = gtk_label_new("Device / Settings");
    gtk_widget_add_css_class(controls_title, "brand");
    gtk_widget_set_halign(controls_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(controls), controls_title);
    if (state->needs_root_warning) {
        GtkWidget *root_warning = gtk_label_new("Run with sudo/root to control DeepCool HID devices.");
        gtk_widget_add_css_class(root_warning, "status");
        gtk_label_set_wrap(GTK_LABEL(root_warning), TRUE);
        gtk_widget_set_halign(root_warning, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(controls), root_warning);
    }

    GtkStringList *dev_list = gtk_string_list_new(NULL);
    if (state->device_count == 0) gtk_string_list_append(dev_list, "No DeepCool device found");
    for (size_t i = 0; i < state->device_count; i++) {
        char item[256];
        snprintf(item, sizeof(item), "%u | %s | %s", state->devices[i].pid, state->devices[i].name, series_name(state->devices[i].series));
        gtk_string_list_append(dev_list, item);
    }
    state->device_combo = drop_down_from_list(dev_list, 0);
    if (state->config.device_index >= 0 && (size_t)state->config.device_index < state->device_count) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(state->device_combo), (guint)state->config.device_index);
    }
    gtk_box_append(GTK_BOX(controls), labeled(state->device_combo, "Device"));

    GtkStringList *gpu_list = gtk_string_list_new(NULL);
    gtk_string_list_append(gpu_list, "None / CPU only");
    for (size_t i = 0; i < state->gpu_count; i++) {
        char item[256];
        snprintf(item, sizeof(item), "%s (%s)", state->gpus[i].name, state->gpus[i].address);
        gtk_string_list_append(gpu_list, item);
    }
    state->gpu_combo = drop_down_from_list(gpu_list, state->gpu_count ? 1 : 0);
    if (state->config.gpu_index >= 0 && (size_t)state->config.gpu_index < state->gpu_count) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(state->gpu_combo), (guint)state->config.gpu_index + 1);
    }
    gtk_box_append(GTK_BOX(controls), labeled(state->gpu_combo, "Monitored GPU"));

    state->mode_combo = drop_down_from_list(string_list_from_presets(display_presets_for_series(SERIES_UNKNOWN)), 0);
    gtk_box_append(GTK_BOX(controls), labeled(state->mode_combo, "Display mode"));

    state->update_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 100, 2000, 100);
    gtk_range_set_value(GTK_RANGE(state->update_scale), state->config.update_ms);
    gtk_scale_set_digits(GTK_SCALE(state->update_scale), 0);
    gtk_box_append(GTK_BOX(controls), labeled(state->update_scale, "Update interval (ms)"));

    state->auto_interval_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1000, 30000, 1000);
    gtk_range_set_value(GTK_RANGE(state->auto_interval_scale), state->config.auto_interval_ms);
    gtk_scale_set_digits(GTK_SCALE(state->auto_interval_scale), 0);
    gtk_box_append(GTK_BOX(controls), labeled(state->auto_interval_scale, "Cycle interval (ms)"));

    const char *rot_items[] = {"0", "90", "180", "270", NULL};
    state->rotate_combo = drop_down_from_list(string_list_from_modes(rot_items), 0);
    gtk_box_append(GTK_BOX(controls), labeled(state->rotate_combo, "LP rotation"));

    GtkWidget *toggles = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(toggles), 8);
    gtk_grid_set_column_spacing(GTK_GRID(toggles), 12);
    state->fahrenheit_switch = gtk_switch_new();
    state->alarm_switch = gtk_switch_new();
    state->zeros_switch = gtk_switch_new();
    state->fahrenheit_label = gtk_label_new("Fahrenheit");
    state->alarm_label = gtk_label_new("High temperature alarm");
    state->zeros_label = gtk_label_new("LD leading zeros");
    gtk_widget_set_halign(state->fahrenheit_label, GTK_ALIGN_START);
    gtk_widget_set_halign(state->alarm_label, GTK_ALIGN_START);
    gtk_widget_set_halign(state->zeros_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(state->fahrenheit_label, "metric-title");
    gtk_widget_add_css_class(state->alarm_label, "metric-title");
    gtk_widget_add_css_class(state->zeros_label, "metric-title");
    gtk_switch_set_active(GTK_SWITCH(state->fahrenheit_switch), state->config.fahrenheit);
    gtk_switch_set_active(GTK_SWITCH(state->alarm_switch), state->config.alarm);
    gtk_switch_set_active(GTK_SWITCH(state->zeros_switch), state->config.lead_zeros);
    gtk_grid_attach(GTK_GRID(toggles), state->fahrenheit_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(toggles), state->fahrenheit_switch, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(toggles), state->alarm_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(toggles), state->alarm_switch, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(toggles), state->zeros_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(toggles), state->zeros_switch, 1, 2, 1, 1);
    gtk_box_append(GTK_BOX(controls), toggles);

    g_signal_connect(state->gpu_combo, "notify::selected", G_CALLBACK(config_changed), state);
    g_signal_connect(state->mode_combo, "notify::selected", G_CALLBACK(config_changed), state);
    g_signal_connect(state->rotate_combo, "notify::selected", G_CALLBACK(config_changed), state);
    g_signal_connect(state->fahrenheit_switch, "notify::active", G_CALLBACK(config_changed), state);
    g_signal_connect(state->alarm_switch, "notify::active", G_CALLBACK(config_changed), state);
    g_signal_connect(state->zeros_switch, "notify::active", G_CALLBACK(config_changed), state);
    g_signal_connect(state->update_scale, "value-changed", G_CALLBACK(config_value_changed), state);
    g_signal_connect(state->auto_interval_scale, "value-changed", G_CALLBACK(config_value_changed), state);

    GtkWidget *main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(main, "main-panel");
    gtk_widget_set_hexpand(main, TRUE);
    gtk_widget_set_vexpand(main, TRUE);
    gtk_box_append(GTK_BOX(body), main);

    GtkWidget *title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *title = gtk_label_new("Monitor");
    gtk_widget_add_css_class(title, "brand");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(title_row), title);
    state->status_label = gtk_label_new("Ready");
    gtk_widget_add_css_class(state->status_label, "status");
    gtk_widget_set_halign(state->status_label, GTK_ALIGN_END);
    gtk_widget_set_hexpand(state->status_label, TRUE);
    gtk_box_append(GTK_BOX(title_row), state->status_label);
    gtk_box_append(GTK_BOX(main), title_row);

    char *cpu = cpu_name();
    state->cpu_label = gtk_label_new(cpu);
    g_free(cpu);
    gtk_widget_set_visible(state->cpu_label, FALSE);

    state->content_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(state->content_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(state->content_stack, TRUE);
    gtk_widget_set_vexpand(state->content_stack, TRUE);
    gtk_box_append(GTK_BOX(main), state->content_stack);

    GtkWidget *monitor_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_stack_add_named(GTK_STACK(state->content_stack), monitor_page, "monitor");

    GtkWidget *cards = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(cards), 14);
    gtk_grid_set_row_spacing(GTK_GRID(cards), 14);
    gtk_widget_set_hexpand(cards, TRUE);
    gtk_box_append(GTK_BOX(monitor_page), cards);

    state->cpu_gauge = gtk_drawing_area_new();
    gtk_widget_set_size_request(state->cpu_gauge, 240, 150);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->cpu_gauge), draw_cpu_gauge, state, NULL);
    GtkWidget *cpu_metrics = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(cpu_metrics), metric_stack("CPU Clock Speed", &state->cpu_freq_label));
    gtk_box_append(GTK_BOX(cpu_metrics), metric_stack("CPU Temperature", &state->cpu_temp_label));
    gtk_box_append(GTK_BOX(cpu_metrics), metric_stack("CPU Power", &state->cpu_power_label));
    GtkWidget *cpu_card = system_card("▣ CPU", "Processor", state->cpu_gauge, cpu_metrics);
    gtk_grid_attach(GTK_GRID(cards), cpu_card, 0, 0, 1, 1);

    state->gpu_gauge = gtk_drawing_area_new();
    gtk_widget_set_size_request(state->gpu_gauge, 240, 150);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->gpu_gauge), draw_gpu_gauge, state, NULL);
    GtkWidget *gpu_metrics = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(gpu_metrics), metric_stack("GPU Clock Speed", &state->gpu_freq_label));
    gtk_box_append(GTK_BOX(gpu_metrics), metric_stack("GPU Temperature", &state->gpu_temp_label));
    gtk_box_append(GTK_BOX(gpu_metrics), metric_stack("GPU Power", &state->gpu_power_label));
    GtkWidget *gpu_card = system_card("▤ GPU", "Graphics", state->gpu_gauge, gpu_metrics);
    gtk_grid_attach(GTK_GRID(cards), gpu_card, 1, 0, 1, 1);

    GtkWidget *device_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_add_css_class(device_card, "device-card");
    gtk_grid_attach(GTK_GRID(cards), device_card, 0, 1, 2, 1);
    GtkWidget *device_index = gtk_label_new("01");
    gtk_widget_add_css_class(device_index, "big-load");
    gtk_box_append(GTK_BOX(device_card), device_index);
    GtkWidget *device_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(device_text, TRUE);
    state->device_name_label = gtk_label_new("DeepCool Digital Device");
    gtk_widget_add_css_class(state->device_name_label, "brand");
    gtk_widget_set_halign(state->device_name_label, GTK_ALIGN_START);
    state->device_kind_label = gtk_label_new("Linux Control Panel");
    gtk_widget_add_css_class(state->device_kind_label, "metric-title");
    gtk_widget_set_halign(state->device_kind_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(device_text), state->device_name_label);
    gtk_box_append(GTK_BOX(device_text), state->device_kind_label);
    gtk_box_append(GTK_BOX(device_card), device_text);
    state->fan_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(state->fan_area, 130, 120);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->fan_area), draw_fan, state, NULL);
    gtk_box_append(GTK_BOX(device_card), state->fan_area);
    GtkWidget *fan_metrics = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(fan_metrics), metric_stack("CPU Fan Speed", &state->cpu_fan_label));
    gtk_box_append(GTK_BOX(fan_metrics), metric_stack("CPU Usage", &state->cpu_usage_label));
    gtk_box_append(GTK_BOX(fan_metrics), metric_stack("GPU Usage", &state->gpu_usage_label));
    gtk_box_append(GTK_BOX(device_card), fan_metrics);

    GtkWidget *device_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_stack_add_named(GTK_STACK(state->content_stack), device_page, "device");
    GtkWidget *device_page_title = gtk_label_new("Device");
    gtk_widget_add_css_class(device_page_title, "brand");
    gtk_widget_set_halign(device_page_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(device_page), device_page_title);
    GtkWidget *device_hint = gtk_label_new("Select a device in the left panel to configure the display. Changes are applied automatically.");
    gtk_widget_add_css_class(device_hint, "metric-title");
    gtk_widget_set_halign(device_hint, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(device_page), device_hint);
    GtkWidget *device_list_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(device_list_card, "device-card");
    gtk_box_append(GTK_BOX(device_page), device_list_card);
    for (size_t i = 0; i < state->device_count; i++) {
        char item[256], caps[256];
        capability_summary(state->devices[i].series, caps, sizeof(caps));
        snprintf(item, sizeof(item), "%02zu  %s  ·  %s", i + 1, state->devices[i].name, series_name(state->devices[i].series));
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *row = gtk_label_new(item);
        gtk_widget_add_css_class(row, "section-title");
        gtk_widget_set_halign(row, GTK_ALIGN_START);
        GtkWidget *cap_label = gtk_label_new(caps);
        gtk_widget_add_css_class(cap_label, "metric-title");
        gtk_widget_set_halign(cap_label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(row_box), row);
        gtk_box_append(GTK_BOX(row_box), cap_label);
        gtk_box_append(GTK_BOX(device_list_card), row_box);
    }
    if (state->device_count == 0) {
        GtkWidget *row = gtk_label_new("No DeepCool device found");
        gtk_widget_add_css_class(row, "section-title");
        gtk_widget_set_halign(row, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(device_list_card), row);
    }

    GtkWidget *settings_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_stack_add_named(GTK_STACK(state->content_stack), settings_page, "settings");
    GtkWidget *settings_title = gtk_label_new("Settings");
    gtk_widget_add_css_class(settings_title, "brand");
    gtk_widget_set_halign(settings_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(settings_page), settings_title);
    GtkWidget *settings_hint = gtk_label_new("Device options are in the left panel. Available modes follow the selected device family exactly.");
    gtk_widget_add_css_class(settings_hint, "metric-title");
    gtk_label_set_wrap(GTK_LABEL(settings_hint), TRUE);
    gtk_widget_set_halign(settings_hint, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(settings_page), settings_hint);
    GtkWidget *settings_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(settings_card, "card");
    gtk_box_append(GTK_BOX(settings_page), settings_card);
    GtkWidget *service_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(service_row, "service-row");
    GtkWidget *service_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *service_title = gtk_label_new("Run in background on login");
    gtk_widget_add_css_class(service_title, "section-title");
    gtk_widget_set_halign(service_title, GTK_ALIGN_START);
    GtkWidget *service_desc = gtk_label_new("Creates deepcool-digital.service using the current saved settings.");
    gtk_widget_add_css_class(service_desc, "metric-title");
    gtk_label_set_wrap(GTK_LABEL(service_desc), TRUE);
    gtk_widget_set_halign(service_desc, GTK_ALIGN_START);
    state->service_status_label = gtk_label_new("Background service is disabled");
    gtk_widget_add_css_class(state->service_status_label, "status");
    gtk_widget_set_halign(state->service_status_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(service_text), service_title);
    gtk_box_append(GTK_BOX(service_text), service_desc);
    gtk_box_append(GTK_BOX(service_text), state->service_status_label);
    gtk_widget_set_hexpand(service_text, TRUE);
    state->service_switch = gtk_switch_new();
    gtk_widget_add_css_class(state->service_switch, "service-switch");
    gtk_widget_set_size_request(state->service_switch, 46, 24);
    gtk_widget_set_valign(state->service_switch, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(service_row), service_text);
    gtk_box_append(GTK_BOX(service_row), state->service_switch);
    gtk_box_append(GTK_BOX(settings_card), service_row);
    gtk_box_append(GTK_BOX(settings_card), gtk_label_new("Administrator permission is requested with pkexec when enabling or disabling the background service."));
    gtk_box_append(GTK_BOX(settings_card), gtk_label_new("Monitoring: CPU/GPU data comes from /sys, /proc, and NVML when available."));
    gtk_box_append(GTK_BOX(settings_card), gtk_label_new("Background mode sends a desktop notification when it starts and keeps the device running without a window."));
    g_signal_connect(state->service_switch, "state-set", G_CALLBACK(service_switch_changed), state);
    update_service_status(state);

    gtk_stack_set_visible_child_name(GTK_STACK(state->content_stack), "monitor");

    state->suppress_config_events = true;
    g_signal_connect(state->device_combo, "notify::selected", G_CALLBACK(device_changed), state);
    update_device_controls(state);
    select_display_preset(state);
    select_drop_down_string(state->rotate_combo, state->config.rotate == 90 ? "90" : state->config.rotate == 180 ? "180" : state->config.rotate == 270 ? "270" : "0");
    state->suppress_config_events = false;
    g_timeout_add(250, ui_refresh, state);
    g_idle_add(restart_worker_cb, state);
    gtk_window_present(GTK_WINDOW(win));
}
