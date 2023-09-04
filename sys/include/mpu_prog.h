extern unsigned int Image$$ER_ROM$$Base;
extern unsigned int Image$$ER_ROM$$Limit;
extern unsigned int Image$$RW_RAM$$Base;
extern unsigned int Image$$ARM_LIB_STACK$$ZI$$Limit;

int setMPU(unsigned int region, unsigned int baseAddress, unsigned int limitAddress, unsigned int rw, unsigned int ex);

