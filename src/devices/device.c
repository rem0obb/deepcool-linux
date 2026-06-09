#include "device.h"

#include <glib.h>
#include <hidapi/hidapi.h>
#include <string.h>

DeviceSeries series_for_pid(uint16_t vid, uint16_t pid) {
    if (vid == CH510_VENDOR_ID && pid == CH510_PRODUCT_ID) return SERIES_CH510;
    if (pid >= 1 && pid <= 4) return SERIES_AK;
    if (pid == 6) return SERIES_LS;
    if (pid == 8) return SERIES_AG;
    if (pid == 10) return SERIES_LD;
    if (pid == 12) return SERIES_LP;
    if (pid == 13 || pid == 15 || pid == 31 || pid == 41 || pid == 42 || pid == 43 || pid == 44) return SERIES_LQ;
    if (pid == 16) return SERIES_AK400_PRO;
    if (pid == 17 || pid == 18) return SERIES_AK620_PRO;
    if (pid == 19 || pid == 22 || pid == 27) return SERIES_CH_GEN2;
    if (pid == 5 || pid == 7 || pid == 21) return SERIES_CH;
    return SERIES_UNKNOWN;
}

const char *series_name(DeviceSeries series) {
    switch (series) {
    case SERIES_AG: return "AG Series";
    case SERIES_AK: return "AK Series";
    case SERIES_LS: return "LS Series";
    case SERIES_LD: return "LD Series";
    case SERIES_LP: return "LP Series";
    case SERIES_LQ: return "LQ/NYX/ASSASSIN IV";
    case SERIES_AK400_PRO: return "AK400 PRO";
    case SERIES_AK620_PRO: return "AK500/AK620 PRO";
    case SERIES_CH: return "CH Series";
    case SERIES_CH_GEN2: return "CH Gen 2";
    case SERIES_CH510: return "CH510";
    default: return "Unsupported";
    }
}

size_t scan_devices(DeepCoolDevice *out, size_t max) {
    if (hid_init() != 0) return 0;
    struct hid_device_info *list = hid_enumerate(0, 0);
    size_t count = 0;
    for (struct hid_device_info *d = list; d && count < max; d = d->next) {
        bool deepcool = d->vendor_id == DEFAULT_VENDOR_ID || (d->vendor_id == CH510_VENDOR_ID && d->product_id == CH510_PRODUCT_ID);
        if (!deepcool) continue;
        out[count].vid = d->vendor_id;
        out[count].pid = d->product_id;
        out[count].series = series_for_pid(d->vendor_id, d->product_id);
        if (d->product_string) {
            char *name = g_ucs4_to_utf8((gunichar *)d->product_string, -1, NULL, NULL, NULL);
            g_strlcpy(out[count].name, name ? name : "DeepCool Digital", sizeof(out[count].name));
            g_free(name);
        } else if (out[count].series == SERIES_CH510) {
            g_strlcpy(out[count].name, "CH510-MESH-DIGITAL", sizeof(out[count].name));
        } else {
            g_strlcpy(out[count].name, "DeepCool Digital", sizeof(out[count].name));
        }
        count++;
    }
    hid_free_enumeration(list);
    return count;
}

Mode effective_mode(DeviceSeries series, Mode mode) {
    if (mode != MODE_DEFAULT) return mode;
    switch (series) {
    case SERIES_AG:
    case SERIES_AK:
    case SERIES_LS:
    case SERIES_CH: return MODE_CPU_TEMP;
    case SERIES_LP: return MODE_CPU_USAGE;
    case SERIES_CH_GEN2: return MODE_CPU_FREQ;
    case SERIES_CH510: return MODE_CPU;
    default: return MODE_AUTO;
    }
}

bool mode_supported(DeviceSeries series, Mode mode, bool secondary) {
    if (mode == MODE_DEFAULT) return true;
    switch (series) {
    case SERIES_AG:
    case SERIES_AK: return !secondary && (mode == MODE_AUTO || mode == MODE_CPU_TEMP || mode == MODE_CPU_USAGE);
    case SERIES_LS: return !secondary && (mode == MODE_AUTO || mode == MODE_CPU_TEMP || mode == MODE_CPU_POWER);
    case SERIES_LD:
    case SERIES_LQ:
    case SERIES_AK400_PRO:
    case SERIES_AK620_PRO: return !secondary && (mode == MODE_AUTO || mode == MODE_DEFAULT);
    case SERIES_LP:
        return mode == MODE_CPU_USAGE || mode == MODE_CPU_TEMP || mode == MODE_CPU_POWER || mode == MODE_GPU_USAGE || mode == MODE_GPU_TEMP || mode == MODE_GPU_POWER;
    case SERIES_CH:
        return secondary ? (mode == MODE_DEFAULT || mode == MODE_GPU_TEMP || mode == MODE_GPU_USAGE) : (mode == MODE_AUTO || mode == MODE_CPU_TEMP || mode == MODE_CPU_USAGE);
    case SERIES_CH_GEN2:
        return !secondary && (mode == MODE_AUTO || mode == MODE_CPU_FREQ || mode == MODE_CPU_FAN || mode == MODE_GPU || mode == MODE_PSU);
    case SERIES_CH510:
        return !secondary && (mode == MODE_CPU || mode == MODE_GPU);
    default:
        return false;
    }
}

const char *const *primary_modes_for_series(DeviceSeries series) {
    static const char *const ag_ak[] = {"", "auto", "cpu_temp", "cpu_usage", NULL};
    static const char *const ls[] = {"", "auto", "cpu_temp", "cpu_power", NULL};
    static const char *const auto_only[] = {"", "auto", NULL};
    static const char *const lp[] = {"", "cpu_usage", "cpu_temp", "cpu_power", "gpu_usage", "gpu_temp", "gpu_power", NULL};
    static const char *const ch[] = {"", "auto", "cpu_temp", "cpu_usage", NULL};
    static const char *const ch_gen2[] = {"", "auto", "cpu_freq", "cpu_fan", "gpu", "psu", NULL};
    static const char *const ch510[] = {"", "cpu", "gpu", NULL};
    static const char *const unsupported[] = {"", NULL};

    switch (series) {
    case SERIES_AG:
    case SERIES_AK:
        return ag_ak;
    case SERIES_LS:
        return ls;
    case SERIES_LD:
    case SERIES_LQ:
    case SERIES_AK400_PRO:
    case SERIES_AK620_PRO:
        return auto_only;
    case SERIES_LP:
        return lp;
    case SERIES_CH:
        return ch;
    case SERIES_CH_GEN2:
        return ch_gen2;
    case SERIES_CH510:
        return ch510;
    default:
        return unsupported;
    }
}

const char *const *secondary_modes_for_series(DeviceSeries series) {
    static const char *const none[] = {"", NULL};
    static const char *const lp[] = {"", "cpu_usage", "cpu_temp", "cpu_power", "gpu_usage", "gpu_temp", "gpu_power", NULL};
    static const char *const ch[] = {"", "gpu_temp", "gpu_usage", NULL};

    switch (series) {
    case SERIES_LP:
        return lp;
    case SERIES_CH:
        return ch;
    default:
        return none;
    }
}

const DisplayPreset *display_presets_for_series(DeviceSeries series) {
    static const DisplayPreset ag_ak[] = {
        {"CPU temperature and CPU usage", MODE_AUTO, MODE_DEFAULT},
        {"CPU temperature", MODE_CPU_TEMP, MODE_DEFAULT},
        {"CPU usage", MODE_CPU_USAGE, MODE_DEFAULT},
        {NULL, MODE_DEFAULT, MODE_DEFAULT}
    };
    static const DisplayPreset ls[] = {
        {"CPU temperature and CPU power", MODE_AUTO, MODE_DEFAULT},
        {"CPU temperature", MODE_CPU_TEMP, MODE_DEFAULT},
        {"CPU power", MODE_CPU_POWER, MODE_DEFAULT},
        {NULL, MODE_DEFAULT, MODE_DEFAULT}
    };
    static const DisplayPreset auto_only[] = {
        {"Automatic CPU data", MODE_AUTO, MODE_DEFAULT},
        {NULL, MODE_DEFAULT, MODE_DEFAULT}
    };
    static const DisplayPreset lp[] = {
        {"CPU usage and CPU temperature", MODE_CPU_USAGE, MODE_CPU_TEMP},
        {"CPU temperature and CPU usage", MODE_CPU_TEMP, MODE_CPU_USAGE},
        {"CPU usage", MODE_CPU_USAGE, MODE_DEFAULT},
        {"CPU temperature", MODE_CPU_TEMP, MODE_DEFAULT},
        {"CPU power", MODE_CPU_POWER, MODE_DEFAULT},
        {"GPU usage", MODE_GPU_USAGE, MODE_DEFAULT},
        {"GPU temperature", MODE_GPU_TEMP, MODE_DEFAULT},
        {"GPU power", MODE_GPU_POWER, MODE_DEFAULT},
        {"CPU usage and GPU usage", MODE_CPU_USAGE, MODE_GPU_USAGE},
        {"CPU temperature and GPU temperature", MODE_CPU_TEMP, MODE_GPU_TEMP},
        {"CPU power and GPU power", MODE_CPU_POWER, MODE_GPU_POWER},
        {"GPU usage and GPU temperature", MODE_GPU_USAGE, MODE_GPU_TEMP},
        {NULL, MODE_DEFAULT, MODE_DEFAULT}
    };
    static const DisplayPreset ch[] = {
        {"CPU temperature and GPU temperature", MODE_CPU_TEMP, MODE_GPU_TEMP},
        {"CPU usage and GPU usage", MODE_CPU_USAGE, MODE_GPU_USAGE},
        {"Automatic CPU and GPU data", MODE_AUTO, MODE_DEFAULT},
        {NULL, MODE_DEFAULT, MODE_DEFAULT}
    };
    static const DisplayPreset ch_gen2[] = {
        {"Automatic system data", MODE_AUTO, MODE_DEFAULT},
        {"CPU frequency", MODE_CPU_FREQ, MODE_DEFAULT},
        {"GPU data", MODE_GPU, MODE_DEFAULT},
        {"CPU fan speed", MODE_CPU_FAN, MODE_DEFAULT},
        {"PSU data", MODE_PSU, MODE_DEFAULT},
        {NULL, MODE_DEFAULT, MODE_DEFAULT}
    };
    static const DisplayPreset ch510[] = {
        {"CPU data", MODE_CPU, MODE_DEFAULT},
        {"GPU data", MODE_GPU, MODE_DEFAULT},
        {NULL, MODE_DEFAULT, MODE_DEFAULT}
    };
    static const DisplayPreset unsupported[] = {
        {"No supported display mode", MODE_DEFAULT, MODE_DEFAULT},
        {NULL, MODE_DEFAULT, MODE_DEFAULT}
    };

    switch (series) {
    case SERIES_AG:
    case SERIES_AK:
        return ag_ak;
    case SERIES_LS:
        return ls;
    case SERIES_LD:
    case SERIES_LQ:
    case SERIES_AK400_PRO:
    case SERIES_AK620_PRO:
        return auto_only;
    case SERIES_LP:
        return lp;
    case SERIES_CH:
        return ch;
    case SERIES_CH_GEN2:
        return ch_gen2;
    case SERIES_CH510:
        return ch510;
    default:
        return unsupported;
    }
}
