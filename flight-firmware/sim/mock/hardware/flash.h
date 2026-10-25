#pragma once
#include <stdint.h>
#include <stddef.h>
void flash_range_erase(uint32_t ofs, size_t n);
void flash_range_program(uint32_t ofs, const uint8_t *d, size_t n);
