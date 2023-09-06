#include "cpu.h"

size_t SIZE_TABLE[7] = {0x10, 0x100, 0x1000, 0x4000, 0x7FFFF, 0x1FFF, 0xA00FFFFF};
unsigned int HEAPSIZE = 0x3B600;

int set_sizetable(uint8_t region, size_t size);
