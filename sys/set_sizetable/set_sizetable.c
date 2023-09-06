#include "set_sizetable.h"

int set_sizetable(uint8_t region, size_t size){
    SIZE_TABLE[region] = size;
}
