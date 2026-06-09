#include "cpu.h"

#include "../io_utils.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool file_exists(const char *path) {
    return g_file_test(path, G_FILE_TEST_IS_REGULAR);
}

static bool fan_label_is_preferred(const char *label) {
    if (!label[0]) return false;
    char *lower = g_ascii_strdown(label, -1);
    bool preferred = strstr(lower, "cpu") || strstr(lower, "pump") || strstr(lower, "aio") || strstr(lower, "fan");
    bool gpu = strstr(lower, "gpu") != NULL;
    g_free(lower);
    return preferred && !gpu;
}

static bool cpu_read_jiffies(unsigned long long *total, unsigned long long *idle) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return false;
    char cpu[8];
    unsigned long long user = 0, nice = 0, system = 0, idle_v = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    int read = fscanf(fp, "%7s %llu %llu %llu %llu %llu %llu %llu %llu", cpu, &user, &nice, &system, &idle_v, &iowait, &irq, &softirq, &steal);
    fclose(fp);
    if (read < 8) return false;
    *idle = idle_v + iowait;
    *total = user + nice + system + idle_v + iowait + irq + softirq + steal;
    return true;
}

void cpu_init(CpuMonitor *cpu) {
    memset(cpu, 0, sizeof(*cpu));
    DIR *dir = opendir("/sys/class/hwmon");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (entry->d_name[0] == '.') continue;
            char path[PATH_MAX], name[128];
            snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", entry->d_name);
            if (read_text_file(path, name, sizeof(name)) &&
                (strcmp(name, "asusec") == 0 || strcmp(name, "coretemp") == 0 || strcmp(name, "k10temp") == 0 || strcmp(name, "zenpower") == 0)) {
                snprintf(cpu->temp_sensor, sizeof(cpu->temp_sensor), "/sys/class/hwmon/%s/temp1_input", entry->d_name);
            }
            for (int i = 1; i <= 8; i++) {
                char fan_path[PATH_MAX], label_path[PATH_MAX], label[128] = "";
                snprintf(fan_path, sizeof(fan_path), "/sys/class/hwmon/%s/fan%d_input", entry->d_name, i);
                if (!file_exists(fan_path)) continue;
                snprintf(label_path, sizeof(label_path), "/sys/class/hwmon/%s/fan%d_label", entry->d_name, i);
                read_text_file(label_path, label, sizeof(label));
                if (!cpu->fan_sensor[0] || fan_label_is_preferred(label)) {
                    snprintf(cpu->fan_sensor, sizeof(cpu->fan_sensor), "%s", fan_path);
                    if (fan_label_is_preferred(label)) break;
                }
            }
            if (cpu->temp_sensor[0] && cpu->fan_sensor[0]) break;
        }
        closedir(dir);
    }
    cpu->rapl_max_uj = read_u64_file("/sys/class/powercap/intel-rapl/intel-rapl:0/max_energy_range_uj");
    cpu_read_jiffies(&cpu->prev_total, &cpu->prev_idle);
    cpu->prev_energy = read_u64_file("/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj");
    cpu->prev_energy_time_us = g_get_monotonic_time();
}

char *cpu_name(void) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return g_strdup("Unknown CPU");
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (g_str_has_prefix(line, "model name")) {
            char *colon = strchr(line, ':');
            fclose(fp);
            if (colon) {
                colon++;
                while (*colon == ' ' || *colon == '\t') colon++;
                colon[strcspn(colon, "\r\n")] = 0;
                return g_strdup(colon);
            }
        }
    }
    fclose(fp);
    return g_strdup("Unknown CPU");
}

uint8_t cpu_temp(CpuMonitor *cpu, bool fahrenheit) {
    if (!cpu->temp_sensor[0]) return 0;
    double celsius = (double)read_u64_file(cpu->temp_sensor) / 1000.0;
    return (uint8_t)CLAMP((int)llround(c_to_unit(celsius, fahrenheit)), 0, 255);
}

uint8_t cpu_usage(CpuMonitor *cpu) {
    unsigned long long total = 0, idle = 0;
    if (!cpu_read_jiffies(&total, &idle)) return 0;
    unsigned long long delta_total = total - cpu->prev_total;
    unsigned long long delta_idle = idle - cpu->prev_idle;
    cpu->prev_total = total;
    cpu->prev_idle = idle;
    if (delta_total == 0) return 0;
    return (uint8_t)CLAMP((int)llround((double)(delta_total - delta_idle) * 100.0 / (double)delta_total), 0, 100);
}

uint16_t cpu_power(CpuMonitor *cpu) {
    if (cpu->rapl_max_uj == 0) return 0;
    uint64_t current = read_u64_file("/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj");
    gint64 now = g_get_monotonic_time();
    uint64_t delta = current >= cpu->prev_energy ? current - cpu->prev_energy : (cpu->rapl_max_uj + current) - cpu->prev_energy;
    gint64 elapsed_us = MAX(now - cpu->prev_energy_time_us, 1);
    cpu->prev_energy = current;
    cpu->prev_energy_time_us = now;
    return (uint16_t)CLAMP((int)llround((double)delta / (double)elapsed_us), 0, 65535);
}

uint16_t cpu_freq(void) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return 0;
    char line[256];
    double max_mhz = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (g_str_has_prefix(line, "cpu MHz")) {
            char *colon = strchr(line, ':');
            if (colon) max_mhz = MAX(max_mhz, g_ascii_strtod(colon + 1, NULL));
        }
    }
    fclose(fp);
    return (uint16_t)CLAMP((int)llround(max_mhz), 0, 65535);
}

uint16_t cpu_fan_rpm(CpuMonitor *cpu) {
    if (!cpu->fan_sensor[0]) return 0;
    return (uint16_t)CLAMP((int)read_u64_file(cpu->fan_sensor), 0, 65535);
}
