#ifndef DEEPCOOL_COMMON_H
#define DEEPCOOL_COMMON_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEFAULT_VENDOR_ID 13875
#define CH510_VENDOR_ID 13523
#define CH510_PRODUCT_ID 4352
#define AUTO_MODE_INTERVAL_MS 5000
#define MAX_GPUS 16
#define MAX_DEVICES 32

typedef enum {
    MODE_DEFAULT,
    MODE_AUTO,
    MODE_CPU_TEMP,
    MODE_CPU_USAGE,
    MODE_CPU_POWER,
    MODE_CPU_FREQ,
    MODE_CPU_FAN,
    MODE_GPU_TEMP,
    MODE_GPU_USAGE,
    MODE_GPU_POWER,
    MODE_CPU,
    MODE_GPU,
    MODE_PSU
} Mode;

typedef enum {
    GPU_NONE,
    GPU_AMD,
    GPU_INTEL,
    GPU_NVIDIA
} GpuVendor;

typedef enum {
    SERIES_UNKNOWN,
    SERIES_AG,
    SERIES_AK,
    SERIES_LS,
    SERIES_LD,
    SERIES_LP,
    SERIES_LQ,
    SERIES_AK400_PRO,
    SERIES_AK620_PRO,
    SERIES_CH,
    SERIES_CH_GEN2,
    SERIES_CH510
} DeviceSeries;

typedef struct {
    Mode mode;
    Mode secondary;
    uint32_t update_ms;
    uint32_t auto_interval_ms;
    bool fahrenheit;
    bool alarm;
    uint16_t rotate;
    bool lead_zeros;
    int device_index;
    int gpu_index;
} Config;

const char *mode_symbol(Mode mode);
Mode mode_from_symbol(const char *value);

#endif
