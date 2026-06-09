#include "io_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool read_text_file(const char *path, char *buf, size_t len) {
    FILE *fp = fopen(path, "r");
    if (!fp) return false;
    bool ok = fgets(buf, (int)len, fp) != NULL;
    fclose(fp);
    if (ok) buf[strcspn(buf, "\r\n")] = 0;
    return ok;
}

uint64_t read_u64_file(const char *path) {
    char buf[64];
    if (!read_text_file(path, buf, sizeof(buf))) return 0;
    return strtoull(buf, NULL, 10);
}

double c_to_unit(double celsius, bool fahrenheit) {
    return fahrenheit ? celsius * 9.0 / 5.0 + 32.0 : celsius;
}
