#ifndef DEEPCOOL_DEVICE_H
#define DEEPCOOL_DEVICE_H

#include "../common.h"

typedef struct {
    uint16_t vid;
    uint16_t pid;
    char name[128];
    DeviceSeries series;
} DeepCoolDevice;

typedef struct {
    const char *label;
    Mode primary;
    Mode secondary;
} DisplayPreset;

DeviceSeries series_for_pid(uint16_t vid, uint16_t pid);
const char *series_name(DeviceSeries series);
size_t scan_devices(DeepCoolDevice *out, size_t max);
Mode effective_mode(DeviceSeries series, Mode mode);
bool mode_supported(DeviceSeries series, Mode mode, bool secondary);
const char *const *primary_modes_for_series(DeviceSeries series);
const char *const *secondary_modes_for_series(DeviceSeries series);
const DisplayPreset *display_presets_for_series(DeviceSeries series);

#endif
