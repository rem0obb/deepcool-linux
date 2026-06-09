#ifndef DEEPCOOL_IO_UTILS_H
#define DEEPCOOL_IO_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool read_text_file(const char *path, char *buf, size_t len);
uint64_t read_u64_file(const char *path);
double c_to_unit(double celsius, bool fahrenheit);

#endif
