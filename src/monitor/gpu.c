#include "gpu.h"

#include "../io_utils.h"

#include <dirent.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static GpuVendor vendor_from_driver(const char *driver, const char *pci_id) {
    if (strcmp(driver, "amdgpu") == 0) return GPU_AMD;
    if (strcmp(driver, "nvidia") == 0) return GPU_NVIDIA;
    if (strcmp(driver, "xe") == 0) return GPU_INTEL;
    if (strcmp(driver, "i915") == 0 && pci_id && strlen(pci_id) >= 7 &&
        (strncmp(pci_id + 5, "56", 2) == 0 || strncmp(pci_id + 5, "E2", 2) == 0)) return GPU_INTEL;
    return GPU_NONE;
}

const char *vendor_name(GpuVendor vendor) {
    switch (vendor) {
    case GPU_AMD: return "AMD";
    case GPU_INTEL: return "Intel";
    case GPU_NVIDIA: return "NVIDIA";
    default: return "None";
    }
}

static uint8_t parse_bus(const char *addr) {
    unsigned int domain = 0, bus = 0, dev = 0, fun = 0;
    if (sscanf(addr, "%x:%x:%x.%x", &domain, &bus, &dev, &fun) == 4) return (uint8_t)bus;
    return 0;
}

size_t scan_gpus(PciGpu *out, size_t max) {
    DIR *dir = opendir("/sys/bus/pci/devices");
    if (!dir) return 0;
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) && count < max) {
        if (entry->d_name[0] == '.') continue;
        char path[PATH_MAX], line[256], driver[64] = "", pci_id[64] = "";
        snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/uevent", entry->d_name);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;
        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\r\n")] = 0;
            if (g_str_has_prefix(line, "DRIVER=")) g_strlcpy(driver, line + 7, sizeof(driver));
            if (g_str_has_prefix(line, "PCI_ID=")) g_strlcpy(pci_id, line + 7, sizeof(pci_id));
        }
        fclose(fp);
        GpuVendor vendor = vendor_from_driver(driver, pci_id);
        if (vendor == GPU_NONE) continue;
        out[count].vendor = vendor;
        out[count].bus = parse_bus(entry->d_name);
        g_strlcpy(out[count].address, entry->d_name, sizeof(out[count].address));
        snprintf(out[count].name, sizeof(out[count].name), "%s %s", vendor_name(vendor), out[count].bus > 0 ? "GPU" : "iGPU");
        count++;
    }
    closedir(dir);
    return count;
}

static bool find_first_child_dir(const char *path, char *out, size_t out_len) {
    DIR *dir = opendir(path);
    if (!dir) return false;
    struct dirent *entry;
    bool ok = false;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        snprintf(out, out_len, "%s/%s", path, entry->d_name);
        ok = true;
        break;
    }
    closedir(dir);
    return ok;
}

static bool file_exists(const char *path) {
    return g_file_test(path, G_FILE_TEST_IS_REGULAR);
}

static void set_if_exists(char *dest, size_t dest_len, const char *path) {
    if (file_exists(path)) g_strlcpy(dest, path, dest_len);
}

static bool find_engine_busy_file(const char *drm_dir, char *out, size_t out_len) {
    const char *rel_paths[] = {
        "engine/rcs0/busy",
        "device/engine/rcs0/busy",
        "device/gt/gt0/engines/rcs0/busy",
        "device/tile0/gt0/engines/rcs0/busy",
        NULL
    };
    for (int i = 0; rel_paths[i]; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", drm_dir, rel_paths[i]);
        if (file_exists(path)) {
            g_strlcpy(out, path, out_len);
            return true;
        }
    }
    return false;
}

static bool find_hwmon_by_name(const char *root, const char **names, size_t n_names, char *out, size_t out_len) {
    DIR *dir = opendir(root);
    if (!dir) return false;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char path[PATH_MAX], name[128];
        snprintf(path, sizeof(path), "%s/%s/name", root, entry->d_name);
        if (read_text_file(path, name, sizeof(name))) {
            for (size_t i = 0; i < n_names; i++) {
                if (strstr(name, names[i])) {
                    snprintf(out, out_len, "%s/%s", root, entry->d_name);
                    closedir(dir);
                    return true;
                }
            }
        }
    }
    closedir(dir);
    return false;
}

typedef int (*NvmlInit)(void);
typedef int (*NvmlDeviceGetHandleByPciBusId)(const char *, void **);

void gpu_init(GpuMonitor *gpu, const PciGpu *pci) {
    memset(gpu, 0, sizeof(*gpu));
    if (!pci) return;
    gpu->vendor = pci->vendor;
    g_strlcpy(gpu->pci_address, pci->address, sizeof(gpu->pci_address));
    g_strlcpy(gpu->name, pci->name, sizeof(gpu->name));
    char base[PATH_MAX];
    snprintf(base, sizeof(base), "/sys/bus/pci/devices/%s", pci->address);
    if (pci->vendor == GPU_AMD) {
        snprintf(gpu->usage_file, sizeof(gpu->usage_file), "%s/gpu_busy_percent", base);
        if (!file_exists(gpu->usage_file)) gpu->usage_file[0] = 0;
        char hwmon_root[PATH_MAX], first[PATH_MAX];
        snprintf(hwmon_root, sizeof(hwmon_root), "%s/hwmon", base);
        if (find_first_child_dir(hwmon_root, first, sizeof(first))) g_strlcpy(gpu->hwmon_dir, first, sizeof(gpu->hwmon_dir));
        char drm_root[PATH_MAX];
        snprintf(drm_root, sizeof(drm_root), "%s/drm", base);
        if (find_first_child_dir(drm_root, first, sizeof(first))) {
            g_strlcpy(gpu->drm_dir, first, sizeof(gpu->drm_dir));
            char busy[PATH_MAX];
            snprintf(busy, sizeof(busy), "%s/device/gpu_busy_percent", first);
            set_if_exists(gpu->usage_file, sizeof(gpu->usage_file), busy);
        }
    } else if (pci->vendor == GPU_INTEL) {
        char drm_root[PATH_MAX], first[PATH_MAX];
        snprintf(drm_root, sizeof(drm_root), "%s/drm", base);
        if (find_first_child_dir(drm_root, first, sizeof(first))) g_strlcpy(gpu->drm_dir, first, sizeof(gpu->drm_dir));
        if (gpu->drm_dir[0] && find_engine_busy_file(gpu->drm_dir, gpu->engine_busy_file, sizeof(gpu->engine_busy_file))) {
            gpu->prev_engine_busy = read_u64_file(gpu->engine_busy_file);
            gpu->prev_engine_time_us = g_get_monotonic_time();
        }
        const char *names[] = {"xe", "i915", "intel_arc", "drm", "coretemp"};
        find_hwmon_by_name("/sys/class/hwmon", names, G_N_ELEMENTS(names), gpu->hwmon_dir, sizeof(gpu->hwmon_dir));
    } else if (pci->vendor == GPU_NVIDIA) {
        const char *paths[] = {"libnvidia-ml.so", "libnvidia-ml.so.1", "/usr/lib/x86_64-linux-gnu/libnvidia-ml.so.1", "/usr/lib/libnvidia-ml.so.1", "/usr/lib64/libnvidia-ml.so.1", "/run/opengl-driver/lib/libnvidia-ml.so.1"};
        for (size_t i = 0; i < G_N_ELEMENTS(paths) && !gpu->nvml; i++) gpu->nvml = dlopen(paths[i], RTLD_LAZY);
        if (gpu->nvml) {
            NvmlInit init = (NvmlInit)dlsym(gpu->nvml, "nvmlInit_v2");
            NvmlDeviceGetHandleByPciBusId get_handle = (NvmlDeviceGetHandleByPciBusId)dlsym(gpu->nvml, "nvmlDeviceGetHandleByPciBusId_v2");
            if (init && get_handle && init() == 0) get_handle(pci->address, &gpu->nvml_device);
        }
    }
}

uint8_t gpu_temp(GpuMonitor *gpu, bool fahrenheit) {
    if (gpu->vendor == GPU_NVIDIA && gpu->nvml && gpu->nvml_device) {
        typedef int (*Fn)(void *, unsigned int, unsigned int *);
        Fn fn = (Fn)dlsym(gpu->nvml, "nvmlDeviceGetTemperature");
        unsigned int temp = 0;
        if (fn && fn(gpu->nvml_device, 0, &temp) == 0) return (uint8_t)(fahrenheit ? llround(temp * 9.0 / 5.0 + 32.0) : temp);
    }
    if (!gpu->hwmon_dir[0]) return 0;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/temp1_input", gpu->hwmon_dir);
    double celsius = (double)read_u64_file(path) / 1000.0;
    return (uint8_t)CLAMP((int)llround(c_to_unit(celsius, fahrenheit)), 0, 255);
}

uint8_t gpu_usage(GpuMonitor *gpu) {
    if (gpu->vendor == GPU_NVIDIA && gpu->nvml && gpu->nvml_device) {
        typedef struct { unsigned int gpu; unsigned int memory; } Utilization;
        typedef int (*Fn)(void *, Utilization *);
        Fn fn = (Fn)dlsym(gpu->nvml, "nvmlDeviceGetUtilizationRates");
        Utilization util = {0, 0};
        if (fn && fn(gpu->nvml_device, &util) == 0) return (uint8_t)CLAMP((int)util.gpu, 0, 100);
    }
    if (gpu->vendor == GPU_AMD && gpu->usage_file[0]) return (uint8_t)CLAMP((int)read_u64_file(gpu->usage_file), 0, 100);
    if (gpu->vendor == GPU_INTEL && gpu->drm_dir[0]) {
        if (gpu->engine_busy_file[0]) {
            uint64_t busy = read_u64_file(gpu->engine_busy_file);
            gint64 now = g_get_monotonic_time();
            uint64_t delta_busy = busy >= gpu->prev_engine_busy ? busy - gpu->prev_engine_busy : 0;
            gint64 delta_time = MAX(now - gpu->prev_engine_time_us, 1);
            gpu->prev_engine_busy = busy;
            gpu->prev_engine_time_us = now;
            double busy_us = delta_busy > (uint64_t)delta_time * 100 ? (double)delta_busy / 1000.0 : (double)delta_busy;
            return (uint8_t)CLAMP((int)llround(busy_us * 100.0 / (double)delta_time), 0, 100);
        }
        char cur_path[PATH_MAX], max_path[PATH_MAX];
        snprintf(cur_path, sizeof(cur_path), "%s/device/gt_cur_freq_mhz", gpu->drm_dir);
        snprintf(max_path, sizeof(max_path), "%s/device/gt_max_freq_mhz", gpu->drm_dir);
        uint64_t cur = read_u64_file(cur_path), max = read_u64_file(max_path);
        if (!cur || !max) {
            snprintf(cur_path, sizeof(cur_path), "%s/device/tile0/gt0/freq0/cur_freq", gpu->drm_dir);
            snprintf(max_path, sizeof(max_path), "%s/device/tile0/gt0/freq0/max_freq", gpu->drm_dir);
            cur = read_u64_file(cur_path); max = read_u64_file(max_path);
        }
        if (max) return (uint8_t)CLAMP((int)llround((double)cur * 100.0 / (double)max), 0, 100);
    }
    return 0;
}

uint16_t gpu_power(GpuMonitor *gpu) {
    if (gpu->vendor == GPU_NVIDIA && gpu->nvml && gpu->nvml_device) {
        typedef int (*Fn)(void *, unsigned int *);
        Fn fn = (Fn)dlsym(gpu->nvml, "nvmlDeviceGetPowerUsage");
        unsigned int mw = 0;
        if (fn && fn(gpu->nvml_device, &mw) == 0) return (uint16_t)llround(mw / 1000.0);
    }
    if (!gpu->hwmon_dir[0]) return 0;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/power1_average", gpu->hwmon_dir);
    return (uint16_t)(read_u64_file(path) / 1000000);
}

uint16_t gpu_freq(GpuMonitor *gpu) {
    if (gpu->vendor == GPU_NVIDIA && gpu->nvml && gpu->nvml_device) {
        typedef int (*Fn)(void *, unsigned int, unsigned int *);
        Fn fn = (Fn)dlsym(gpu->nvml, "nvmlDeviceGetClockInfo");
        unsigned int mhz = 0;
        if (fn && fn(gpu->nvml_device, 0, &mhz) == 0) return (uint16_t)mhz;
    }
    if (!gpu->hwmon_dir[0]) return 0;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/freq1_input", gpu->hwmon_dir);
    return (uint16_t)(read_u64_file(path) / 1000000);
}
