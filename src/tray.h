#ifndef DEEPCOOL_TRAY_H
#define DEEPCOOL_TRAY_H

#include <stdbool.h>
#include <stddef.h>

typedef struct TrayIcon TrayIcon;

TrayIcon *tray_icon_start(void);
void tray_icon_stop(TrayIcon *tray);
bool desktop_integration_install(const char *source_exe_path, char *error, size_t error_len);

#endif
