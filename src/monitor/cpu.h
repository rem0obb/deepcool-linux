#ifndef DEEPCOOL_MONITOR_CPU_H
#define DEEPCOOL_MONITOR_CPU_H

#include <glib.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char temp_sensor[PATH_MAX];
    char fan_sensor[PATH_MAX];
    uint64_t rapl_max_uj;
    unsigned long long prev_total;
    unsigned long long prev_idle;
    uint64_t prev_energy;
    gint64 prev_energy_time_us;
} CpuMonitor;

void cpu_init(CpuMonitor *cpu);
char *cpu_name(void);
uint8_t cpu_temp(CpuMonitor *cpu, bool fahrenheit);
uint8_t cpu_usage(CpuMonitor *cpu);
uint16_t cpu_power(CpuMonitor *cpu);
uint16_t cpu_freq(void);
uint16_t cpu_fan_rpm(CpuMonitor *cpu);

#endif
