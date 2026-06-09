#ifndef DEEPCOOL_MONITOR_GPU_H
#define DEEPCOOL_MONITOR_GPU_H

#include "../common.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    GpuVendor vendor;
    uint8_t bus;
    char address[32];
    char name[128];
} PciGpu;

typedef struct {
    GpuVendor vendor;
    char pci_address[32];
    char name[128];
    char usage_file[PATH_MAX];
    char engine_busy_file[PATH_MAX];
    char hwmon_dir[PATH_MAX];
    char drm_dir[PATH_MAX];
    uint64_t prev_engine_busy;
    gint64 prev_engine_time_us;
    void *nvml;
    void *nvml_device;
} GpuMonitor;

const char *vendor_name(GpuVendor vendor);
size_t scan_gpus(PciGpu *out, size_t max);
void gpu_init(GpuMonitor *gpu, const PciGpu *pci);
uint8_t gpu_temp(GpuMonitor *gpu, bool fahrenheit);
uint8_t gpu_usage(GpuMonitor *gpu);
uint16_t gpu_power(GpuMonitor *gpu);
uint16_t gpu_freq(GpuMonitor *gpu);

#endif
