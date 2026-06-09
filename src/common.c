#include "common.h"

#include <string.h>

const char *mode_symbol(Mode mode) {
    switch (mode) {
    case MODE_AUTO: return "auto";
    case MODE_CPU_TEMP: return "cpu_temp";
    case MODE_CPU_USAGE: return "cpu_usage";
    case MODE_CPU_POWER: return "cpu_power";
    case MODE_CPU_FREQ: return "cpu_freq";
    case MODE_CPU_FAN: return "cpu_fan";
    case MODE_GPU_TEMP: return "gpu_temp";
    case MODE_GPU_USAGE: return "gpu_usage";
    case MODE_GPU_POWER: return "gpu_power";
    case MODE_CPU: return "cpu";
    case MODE_GPU: return "gpu";
    case MODE_PSU: return "psu";
    default: return "";
    }
}

Mode mode_from_symbol(const char *value) {
    if (!value) return MODE_DEFAULT;
    if (strcmp(value, "auto") == 0) return MODE_AUTO;
    if (strcmp(value, "cpu_temp") == 0) return MODE_CPU_TEMP;
    if (strcmp(value, "cpu_usage") == 0) return MODE_CPU_USAGE;
    if (strcmp(value, "cpu_power") == 0) return MODE_CPU_POWER;
    if (strcmp(value, "cpu_freq") == 0) return MODE_CPU_FREQ;
    if (strcmp(value, "cpu_fan") == 0) return MODE_CPU_FAN;
    if (strcmp(value, "gpu_temp") == 0) return MODE_GPU_TEMP;
    if (strcmp(value, "gpu_usage") == 0) return MODE_GPU_USAGE;
    if (strcmp(value, "gpu_power") == 0) return MODE_GPU_POWER;
    if (strcmp(value, "cpu") == 0) return MODE_CPU;
    if (strcmp(value, "gpu") == 0) return MODE_GPU;
    if (strcmp(value, "psu") == 0) return MODE_PSU;
    return MODE_DEFAULT;
}
