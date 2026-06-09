#include "protocol.h"

#include <hidapi/hidapi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void put_digits3(uint8_t *data, int offset, uint16_t value) {
    data[offset] = (uint8_t)(value / 100);
    data[offset + 1] = (uint8_t)(value % 100 / 10);
    data[offset + 2] = (uint8_t)(value % 10);
}

static uint8_t status_bar(uint8_t usage) {
    return usage < 15 ? 1 : (uint8_t)CLAMP((int)llround(usage / 10.0), 1, 10);
}

static void be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

static void be_float(uint8_t *p, float value) {
    union { float f; uint8_t b[4]; } u;
    u.f = value;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
    p[0] = u.b[3]; p[1] = u.b[2]; p[2] = u.b[1]; p[3] = u.b[0];
#else
    memcpy(p, u.b, 4);
#endif
}

static uint8_t checksum_range(const uint8_t *data, int first, int last) {
    uint16_t sum = 0;
    for (int i = first; i <= last; i++) sum += data[i];
    return (uint8_t)(sum % 256);
}

static bool sleep_or_stop(AppState *state, uint32_t ms) {
    uint32_t waited = 0;
    while (waited < ms) {
        g_usleep(MIN(50000, (ms - waited) * 1000));
        waited += MIN(50, ms - waited);
        g_mutex_lock(&state->lock);
        bool stop = state->stop_requested;
        g_mutex_unlock(&state->lock);
        if (stop) return true;
    }
    return false;
}

static void publish_metrics(AppState *state, uint8_t ct, uint8_t cu, uint16_t cp, uint16_t cf, uint8_t gt, uint8_t gu, uint16_t gp, uint16_t gf, const uint8_t *packet, size_t packet_len) {
    g_mutex_lock(&state->lock);
    state->cpu_temp = ct;
    state->cpu_usage = cu;
    state->cpu_power = cp;
    state->cpu_freq = cf;
    state->cpu_fan_rpm = cpu_fan_rpm(&state->cpu);
    state->gpu_temp = gt;
    state->gpu_usage = gu;
    state->gpu_power = gp;
    state->gpu_freq = gf;
    if (packet && packet_len) {
        size_t pos = 0, max = MIN(packet_len, 48);
        for (size_t i = 0; i < max && pos + 4 < sizeof(state->last_packet); i++) pos += snprintf(state->last_packet + pos, sizeof(state->last_packet) - pos, "%02X ", packet[i]);
    }
    g_mutex_unlock(&state->lock);
}

static bool write_packet(hid_device *device, const uint8_t *data, size_t len) {
    return hid_write(device, data, len) >= 0;
}

static bool build_simple_packet(AppState *state, DeviceSeries series, Mode mode, uint8_t data[64]) {
    memset(data, 0, 64);
    data[0] = 16;
    uint8_t cu = cpu_usage(&state->cpu);
    uint8_t ct = cpu_temp(&state->cpu, state->config.fahrenheit && series != SERIES_AG);
    uint16_t cp = cpu_power(&state->cpu);
    if (series == SERIES_AG) {
        if (mode == MODE_CPU_USAGE) {
            data[1] = 76; data[3] = cu < 100 ? cu % 100 / 10 : 9; data[4] = cu < 100 ? cu % 10 : 9;
        } else {
            data[1] = 19; data[3] = ct < 100 ? ct % 100 / 10 : 9; data[4] = ct < 100 ? ct % 10 : 9;
        }
        data[5] = state->config.alarm && ct >= 90;
    } else {
        if (mode == MODE_CPU_POWER) {
            data[1] = 76; put_digits3(data, 3, cp);
        } else if (mode == MODE_CPU_USAGE) {
            data[1] = 76; put_digits3(data, 3, cu);
        } else {
            data[1] = state->config.fahrenheit ? 35 : 19; put_digits3(data, 3, ct);
        }
        data[2] = status_bar(cu);
        data[6] = state->config.alarm && ct >= (state->config.fahrenheit ? 194 : 90);
    }
    publish_metrics(state, ct, cu, cp, cpu_freq(), gpu_temp(&state->gpu, state->config.fahrenheit), gpu_usage(&state->gpu), gpu_power(&state->gpu), gpu_freq(&state->gpu), data, 64);
    return true;
}

static bool build_ch_packet(AppState *state, Mode mode, uint8_t data[64]) {
    memset(data, 0, 64);
    data[0] = 16;
    uint8_t cu = cpu_usage(&state->cpu), gu = gpu_usage(&state->gpu);
    uint8_t ct = cpu_temp(&state->cpu, state->config.fahrenheit), gt = gpu_temp(&state->gpu, state->config.fahrenheit);
    if (mode == MODE_CPU_USAGE) {
        data[1] = 76; put_digits3(data, 3, cu);
        if (state->config.secondary == MODE_DEFAULT || state->config.secondary == MODE_GPU_USAGE) { data[6] = 76; put_digits3(data, 8, gu); }
    } else {
        data[1] = state->config.fahrenheit ? 35 : 19; put_digits3(data, 3, ct);
        if (state->config.secondary == MODE_DEFAULT || state->config.secondary == MODE_GPU_TEMP) { data[6] = state->config.fahrenheit ? 35 : 19; put_digits3(data, 8, gt); }
    }
    if (data[6] == 0 && state->config.secondary == MODE_GPU_USAGE) { data[6] = 76; put_digits3(data, 8, gu); }
    if (data[6] == 0 && state->config.secondary == MODE_GPU_TEMP) { data[6] = state->config.fahrenheit ? 35 : 19; put_digits3(data, 8, gt); }
    data[2] = status_bar(cu);
    data[7] = status_bar(gu);
    publish_metrics(state, ct, cu, cpu_power(&state->cpu), cpu_freq(), gt, gu, gpu_power(&state->gpu), gpu_freq(&state->gpu), data, 64);
    return true;
}

static bool build_modern_cpu_packet(AppState *state, DeviceSeries series, uint8_t data[64]) {
    memset(data, 0, 64);
    data[0] = 16; data[1] = 104; data[2] = 1;
    int end = 15, checksum_at = 16, term_at = 17, power_at = 8, temp_unit_at = 10, usage_at = 15, freq_at = 0;
    if (series == SERIES_LD) { data[3] = 1; data[4] = 11; data[5] = 1; data[6] = 2; data[7] = 5; }
    else if (series == SERIES_AK400_PRO) { data[3] = 2; data[4] = 11; data[5] = 1; data[6] = 2; data[7] = 5; }
    else if (series == SERIES_AK620_PRO) { data[3] = 4; data[4] = 13; data[5] = 1; data[6] = 2; data[7] = 8; end = 17; checksum_at = 18; term_at = 19; freq_at = 16; }
    else { data[3] = 8; data[4] = 12; data[5] = 1; data[6] = 2; power_at = 7; temp_unit_at = 9; usage_at = 14; end = 16; checksum_at = 17; term_at = 18; freq_at = 15; }
    uint8_t cu = cpu_usage(&state->cpu), ct = cpu_temp(&state->cpu, state->config.fahrenheit);
    uint16_t cp = cpu_power(&state->cpu), cf = cpu_freq();
    be16(&data[power_at], cp);
    data[temp_unit_at] = state->config.fahrenheit ? 1 : 0;
    be_float(&data[temp_unit_at + 1], (float)ct);
    data[usage_at] = cu;
    if (freq_at) be16(&data[freq_at], cf);
    data[checksum_at] = checksum_range(data, 1, end);
    data[term_at] = 22;
    publish_metrics(state, ct, cu, cp, cf, gpu_temp(&state->gpu, state->config.fahrenheit), gpu_usage(&state->gpu), gpu_power(&state->gpu), gpu_freq(&state->gpu), data, 64);
    return true;
}

static bool build_ch_gen2_packet(AppState *state, Mode mode, uint8_t data[64]) {
    memset(data, 0, 64);
    data[0] = 16; data[1] = 104; data[2] = 1; data[3] = 6; data[4] = 35; data[5] = 1; data[9] = state->config.fahrenheit ? 1 : 0;
    data[6] = mode == MODE_GPU ? 4 : mode == MODE_PSU ? 5 : mode == MODE_CPU_FAN ? 3 : 2;
    uint8_t cu = cpu_usage(&state->cpu), gu = gpu_usage(&state->gpu);
    uint8_t ct = cpu_temp(&state->cpu, state->config.fahrenheit), gt = gpu_temp(&state->gpu, state->config.fahrenheit);
    uint16_t cp = cpu_power(&state->cpu), cf = cpu_freq(), gp = gpu_power(&state->gpu), gf = gpu_freq(&state->gpu);
    if (mode == MODE_GPU) {
        be16(&data[19], gp); be_float(&data[21], (float)gt); data[25] = gu; be16(&data[26], gf);
    } else if (mode != MODE_PSU) {
        be16(&data[7], cp); be_float(&data[10], (float)ct); data[14] = cu;
        if (mode == MODE_CPU_FREQ) be16(&data[15], cf);
    }
    data[40] = checksum_range(data, 1, 39);
    data[41] = 22;
    publish_metrics(state, ct, cu, cp, cf, gt, gu, gp, gf, data, 64);
    return true;
}

static const bool unit_percent[5][5] = {{1,1,0,0,1},{1,1,0,1,0},{0,0,1,0,0},{0,1,0,1,1},{1,0,0,1,1}};
static const bool unit_c[5][5] = {{1,0,0,0,0},{0,0,1,1,0},{0,1,0,0,0},{0,1,0,0,0},{0,0,1,1,0}};
static const bool unit_f[5][5] = {{1,0,1,1,0},{0,0,1,0,0},{0,0,1,1,0},{0,0,1,0,0},{0,0,1,0,0}};
static const bool unit_w[5][5] = {{0,0,0,0,0},{1,0,1,0,1},{1,0,1,0,1},{1,0,1,0,1},{0,1,0,1,0}};
static const bool nums[10][5][3] = {
    {{1,1,1},{1,0,1},{1,0,1},{1,0,1},{1,1,1}}, {{0,1,0},{1,1,0},{0,1,0},{0,1,0},{1,1,1}},
    {{1,1,1},{0,0,1},{0,1,0},{1,0,0},{1,1,1}}, {{1,1,1},{0,0,1},{1,1,1},{0,0,1},{1,1,1}},
    {{1,0,1},{1,0,1},{1,1,1},{0,0,1},{0,0,1}}, {{1,1,1},{1,0,0},{1,1,1},{0,0,1},{1,1,1}},
    {{1,1,1},{1,0,0},{1,1,1},{1,0,1},{1,1,1}}, {{1,1,1},{0,0,1},{0,1,0},{0,1,0},{0,1,0}},
    {{1,1,1},{1,0,1},{1,1,1},{1,0,1},{1,1,1}}, {{1,1,1},{1,0,1},{1,1,1},{0,0,1},{1,1,1}}
};

static void matrix_insert_num(bool m[14][14], uint8_t n, int row, int col) {
    if (n > 9) return;
    for (int r = 0; r < 5 && row + r < 14; r++) for (int c = 0; c < 3 && col + c < 14; c++) m[row + r][col + c] = nums[n][r][c];
}

static void matrix_insert_unit(bool m[14][14], const bool u[5][5], int row, int col) {
    for (int r = 0; r < 5 && row + r < 14; r++) for (int c = 0; c < 5 && col + c < 14; c++) m[row + r][col + c] = u[r][c];
}

static void matrix_rotate(bool m[14][14], uint16_t deg) {
    bool r[14][14] = {{0}};
    for (int i = 0; i < 14; i++) for (int j = 0; j < 14; j++) {
        if (deg == 90) r[j][13 - i] = m[i][j];
        else if (deg == 180) r[13 - i][13 - j] = m[i][j];
        else if (deg == 270) r[13 - j][i] = m[i][j];
        else return;
    }
    memcpy(m, r, sizeof(r));
}

static void matrix_bytes(bool m[14][14], uint8_t out[28]) {
    static const uint8_t row_values[7] = {16, 32, 64, 128, 1, 2, 4};
    memset(out, 0, 28);
    for (int col = 0; col < 14; col++) {
        for (int row = 0; row < 7; row++) if (m[row * 2][col]) out[col] += row_values[row];
        for (int row = 0; row < 7; row++) if (m[row * 2 + 1][col]) out[27 - col] += row_values[row];
    }
}

static uint16_t value_for_mode(AppState *state, Mode mode, const bool **unit) {
    switch (mode) {
    case MODE_CPU_USAGE: *unit = (const bool *)unit_percent; return cpu_usage(&state->cpu);
    case MODE_CPU_TEMP: *unit = (const bool *)(state->config.fahrenheit ? unit_f : unit_c); return cpu_temp(&state->cpu, state->config.fahrenheit);
    case MODE_CPU_POWER: *unit = (const bool *)unit_w; return cpu_power(&state->cpu);
    case MODE_GPU_USAGE: *unit = (const bool *)unit_percent; return gpu_usage(&state->gpu);
    case MODE_GPU_TEMP: *unit = (const bool *)(state->config.fahrenheit ? unit_f : unit_c); return gpu_temp(&state->gpu, state->config.fahrenheit);
    case MODE_GPU_POWER: *unit = (const bool *)unit_w; return gpu_power(&state->gpu);
    default: *unit = (const bool *)unit_percent; return 0;
    }
}

static void lp_insert_value(AppState *state, bool matrix[14][14], int row, Mode mode) {
    const bool *unit_ptr = NULL;
    uint16_t value = value_for_mode(state, mode, &unit_ptr);
    const bool (*unit)[5] = (const bool (*)[5])unit_ptr;
    if (value < 100) {
        matrix_insert_num(matrix, value / 10, row, 1);
        matrix_insert_num(matrix, value % 10, row, 5);
        matrix_insert_unit(matrix, unit, 5, 9);
    } else {
        matrix_insert_num(matrix, value / 100, row, 1);
        matrix_insert_num(matrix, value % 100 / 10, row, 5);
        matrix_insert_num(matrix, value % 10, row, 9);
        matrix_insert_unit(matrix, unit, 5, 13);
    }
}

static bool build_lp_packet(AppState *state, uint8_t data[64]) {
    memset(data, 0, 64);
    data[0] = 16; data[1] = 104; data[2] = 1; data[3] = 5; data[4] = 29; data[5] = 1;
    bool matrix[14][14] = {{0}};
    if (state->config.secondary != MODE_DEFAULT) {
        lp_insert_value(state, matrix, 1, state->config.mode);
        lp_insert_value(state, matrix, 8, state->config.secondary);
    } else {
        lp_insert_value(state, matrix, 5, state->config.mode);
    }
    if (state->config.rotate) matrix_rotate(matrix, state->config.rotate);
    matrix_bytes(matrix, &data[6]);
    data[34] = checksum_range(data, 1, 33);
    data[35] = 22;
    publish_metrics(state, cpu_temp(&state->cpu, state->config.fahrenheit), cpu_usage(&state->cpu), cpu_power(&state->cpu), cpu_freq(),
                    gpu_temp(&state->gpu, state->config.fahrenheit), gpu_usage(&state->gpu), gpu_power(&state->gpu), gpu_freq(&state->gpu), data, 64);
    return true;
}

gpointer device_worker_main(gpointer user_data) {
    AppState *state = user_data;
    Config cfg = state->config;
    DeepCoolDevice dev = state->devices[cfg.device_index];
    cpu_init(&state->cpu);
    gpu_init(&state->gpu, cfg.gpu_index >= 0 && (size_t)cfg.gpu_index < state->gpu_count ? &state->gpus[cfg.gpu_index] : NULL);
    hid_device *hid = hid_open(dev.vid, dev.pid, NULL);
    if (!hid) {
        g_mutex_lock(&state->lock);
        snprintf(state->status, sizeof(state->status), "Failed to access the USB device. Run as root or configure udev.");
        state->running = false;
        g_mutex_unlock(&state->lock);
        return NULL;
    }
    uint8_t data[64] = {0};
    if (dev.series == SERIES_AK || dev.series == SERIES_LS) {
        data[0] = 16; data[1] = 170; write_packet(hid, data, 64);
    } else if (dev.series == SERIES_LD) {
        uint8_t init[64] = {16, 104, 1, 1, 2, 3, 1, 112, 22};
        write_packet(hid, init, 64);
        init[5] = 2;
        if (cfg.lead_zeros) init[7] = 111; else { init[6] = 0; init[7] = 110; }
        write_packet(hid, init, 64);
    }
    g_mutex_lock(&state->lock);
    snprintf(state->status, sizeof(state->status), "Sending data to %s (%s)", dev.name, series_name(dev.series));
    g_mutex_unlock(&state->lock);
    Mode mode = effective_mode(dev.series, cfg.mode);
    Mode auto_mode = mode == MODE_AUTO ? effective_mode(dev.series, MODE_DEFAULT) : mode;
    uint32_t auto_interval = cfg.auto_interval_ms ? cfg.auto_interval_ms : AUTO_MODE_INTERVAL_MS;
    gint64 next_auto = g_get_monotonic_time() + (gint64)auto_interval * 1000;
    while (true) {
        g_mutex_lock(&state->lock);
        bool stop = state->stop_requested;
        g_mutex_unlock(&state->lock);
        if (stop) break;
        if (mode == MODE_AUTO && g_get_monotonic_time() >= next_auto) {
            if (dev.series == SERIES_LS) auto_mode = auto_mode == MODE_CPU_TEMP ? MODE_CPU_POWER : MODE_CPU_TEMP;
            else if (dev.series == SERIES_CH_GEN2) auto_mode = auto_mode == MODE_CPU_FREQ ? MODE_GPU : MODE_CPU_FREQ;
            else auto_mode = auto_mode == MODE_CPU_TEMP ? MODE_CPU_USAGE : MODE_CPU_TEMP;
            next_auto = g_get_monotonic_time() + (gint64)auto_interval * 1000;
        }
        if (sleep_or_stop(state, cfg.update_ms)) break;
        bool ok = true;
        if (dev.series == SERIES_CH510) {
            uint8_t usage = auto_mode == MODE_GPU ? gpu_usage(&state->gpu) : cpu_usage(&state->cpu);
            uint8_t temp = auto_mode == MODE_GPU ? gpu_temp(&state->gpu, cfg.fahrenheit) : cpu_temp(&state->cpu, cfg.fahrenheit);
            char msg[64];
            snprintf(msg, sizeof(msg), "HLXDATA(%u,%u,0,0,%c)\r\n", usage, temp, cfg.fahrenheit ? 'F' : 'C');
            ok = hid_write(hid, (const unsigned char *)msg, strlen(msg)) >= 0;
            publish_metrics(state, cpu_temp(&state->cpu, cfg.fahrenheit), cpu_usage(&state->cpu), cpu_power(&state->cpu), cpu_freq(),
                            gpu_temp(&state->gpu, cfg.fahrenheit), gpu_usage(&state->gpu), gpu_power(&state->gpu), gpu_freq(&state->gpu), (const uint8_t *)msg, strlen(msg));
        } else if (dev.series == SERIES_AG || dev.series == SERIES_AK || dev.series == SERIES_LS) {
            build_simple_packet(state, dev.series, auto_mode, data); ok = write_packet(hid, data, 64);
        } else if (dev.series == SERIES_CH) {
            build_ch_packet(state, auto_mode, data); ok = write_packet(hid, data, 64);
        } else if (dev.series == SERIES_LD || dev.series == SERIES_LQ || dev.series == SERIES_AK400_PRO || dev.series == SERIES_AK620_PRO) {
            build_modern_cpu_packet(state, dev.series, data); ok = write_packet(hid, data, 64);
        } else if (dev.series == SERIES_CH_GEN2) {
            build_ch_gen2_packet(state, auto_mode, data); ok = write_packet(hid, data, 64);
        } else if (dev.series == SERIES_LP) {
            build_lp_packet(state, data); ok = write_packet(hid, data, 64);
        } else {
            g_mutex_lock(&state->lock);
            snprintf(state->status, sizeof(state->status), "Device found, but PID %u is not supported yet.", dev.pid);
            g_mutex_unlock(&state->lock);
            break;
        }
        if (!ok) {
            g_mutex_lock(&state->lock);
            snprintf(state->status, sizeof(state->status), "Failed to write to the HID device.");
            g_mutex_unlock(&state->lock);
            break;
        }
    }
    hid_close(hid);
    g_mutex_lock(&state->lock);
    state->running = false;
    state->stop_requested = false;
    snprintf(state->status, sizeof(state->status), "Idle");
    g_mutex_unlock(&state->lock);
    return NULL;
}
