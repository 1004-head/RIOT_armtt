/*
 * Copyright (C) 2019 Gunar Schorcht
 *               2022 Otto-von-Guericke-Universität Magdeburg
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @{
 *
 * @file
 * @brief   Implements various POSIX syscalls
 * @author  Gunar Schorcht <gunar@schorcht.net>
 * @author  Marian Buschsieweke <marian.buschsieweke@ovgu.de>
 */

#include <stdio.h>
#include <string.h>

#include "architecture.h"
#include "assert.h"
#include "cpu.h"
#include "irq.h"
#include "kernel_defines.h"
#include "mutex.h"

/*
 * mpu set header file
 */
#include "mpu_prog.h"
#include "mpu_defs.h"

extern void *__real_malloc(size_t size);
extern void __real_free(void *ptr);
extern void *__real_realloc(void *ptr, size_t size);

static mutex_t _lock;
bool zerofat_malloc_inited = false; //checking allocator is initiated

/*
 * MPU region info
 */
static unsigned int ROMADDRESS = 0x08000000;
static unsigned int ROMLIMIT = 0x0807FFFF;
static unsigned int DATAADDRESS = 0x20000000;
static unsigned int DATALIMIT = 0x20001FFF;
static unsigned int STACKADDRESS = 0x2003C80;
static unsigned int STACKLIMIT = 0x2003E7F;
static unsigned int PERIPHADDRESS = 0x40000000;
static unsigned int PERIPHLIMIT = 0x5FFFFFFF;

unsigned int HEAPSIZE = 9600;
/*
 * zerofat data structure
 */
struct zerofat_freelist_s
{
  uintptr_t _reserved;
  struct zerofat_freelist_s *next;
};
typedef struct zerofat_freelist_s *zerofat_freelist_t;

struct zerofat_regioninfo_s
{
  zerofat_freelist_t freelist;
  void *freeptr;
  void *endptr;
  void *accessptr;
};
typedef struct zerofat_regioninfo_s *zerofat_regioninfo_t;

struct zerofat_regioninfo_s ZEROFAT_REGION_INFO[4];

/*
 * initialize mpu region
 */
void init_MPU(){
  setMPU(5UL, ROMADDRESS, ROMLIMIT, ARM_MPU_RO, ARM_MPU_EXEC); //ROM set region 5
  setMPU(6UL, DATAADDRESS, DATALIMIT, ARM_MPU_RW, ARM_MPU_XN); //data, bss set region 6
  setMPU(7UL, STACKADDRESS, STACKLIMIT, ARM_MPU_RW, ARM_MPU_XN); //stack set region 7
  setMPU(8UL, PERIPHADDRESS, PERIPHLIMIT, ARM_MPU_RW, ARM_MPU_XN); //peripheral set region 8
}

/*
 * zerofat initialize mpu region, data structure
 */
bool zerofat_init(void)
{
  init_MPU();
  printf("MPU Initializing is done\n");
  for(size_t i=0; i<4; i++)
  {
    uint8_t *heapptr = 0x20002000 + (HEAPSIZE/4)*i; //zerofat heep ptr
    uint8_t *startptr = 0x20002000 + (HEAPSIZE/4)*i; //zerofat region start ptr
    zerofat_regioninfo_t info = ZEROFAT_REGION_INFO + i; //zerofat region info
    info->freelist = NULL;
    info->freeptr = startptr;
    info->endptr = heapptr + HEAPSIZE/4; //region size
    //info->accessptr = //zerofat region ptr
    setMPU(i+1, heapptr, heapptr+HEAPSIZE, ARM_MPU_RW, ARM_MPU_XN); //heap set region 1, 2, 3, 4
  }
  zerofat_malloc_inited = true;
  printf("Zerofat Initializing is done\n");

  return true;
}

/*
 * zerofat allocator malloc
 */
void __attribute__((used)) *__wrap_malloc(size_t size)
{
    if(!zerofat_malloc_inited)
      zerofat_init();
    
    size_t alloc_size = 8;//zerofat alloc region size
    zerofat_regioninfo_t info = &ZEROFAT_REGION_INFO[0]; //zerofat region info
    void *ptr;

    uinttxtptr_t pc;
    if (IS_USED(MODULE_MALLOC_TRACING)) {
        pc = cpu_get_caller_pc();
    }
    assert(!irq_is_in());
    mutex_lock(&_lock);
    
    zerofat_freelist_t freelist = info->freelist;
    if(freelist != NULL)
    {
      info->freelist = freelist->next;
      ptr = (void *)freelist;
      mutex_unlock(&_lock);
      return ptr;
    }
    ptr = info->freeptr;
    void *freeptr = (uint8_t *)ptr + alloc_size;
    if(freeptr > info->endptr)
    {
      ptr = __real_malloc(alloc_size);
      mutex_unlock(&_lock);
      return ptr;
    }
    info->freeptr = freeptr;

    ptr = __real_malloc(size);
    printf("zerofat ptr is %p\n", ptr);
    mutex_unlock(&_lock);
    if (IS_USED(MODULE_MALLOC_TRACING)) {
        printf("malloc(%u) @ 0x%" PRIxTXTPTR " returned %p\n",
               (unsigned)size, pc, ptr);
    }
    return ptr;
}

void __attribute__((used)) __wrap_free(void *ptr)
{
    if (ptr == NULL){
      printf("ptr is NULL\n");
      return;
    }

    /*
    if(!zerofat_is_ptr(ptr))
    {
      __real_free(ptr);
      return;
    }
    */

    size_t alloc_size = 8;
    zerofat_regioninfo_t info = &ZEROFAT_REGION_INFO[0];

    if (IS_USED(MODULE_MALLOC_TRACING)) {
        uinttxtptr_t pc = cpu_get_caller_pc();
        printf("free(%p) @0x%" PRIxTXTPTR ")\n", ptr, pc);
    }
    assert(!irq_is_in());
    mutex_lock(&_lock);

    zerofat_freelist_t newfreelist = (zerofat_freelist_t)ptr;
    zerofat_freelist_t oldfreelist = info->freelist;
    newfreelist->next = oldfreelist;
    info->freelist = newfreelist;
    mutex_unlock(&_lock);

    printf("zerofat ptr free is done\n");
}

void * __attribute__((used)) __wrap_calloc(size_t nmemb, size_t size)
{
    uinttxtptr_t pc;
    if (IS_USED(MODULE_MALLOC_TRACING)) {
        pc = cpu_get_caller_pc();
    }
    /* some c libs don't perform proper overflow check (e.g. newlib < 4.0.0). Hence, we
     * just implement calloc on top of malloc ourselves. In addition to ensuring proper
     * overflow checks, this likely saves a bit of ROM */
    size_t total_size;
    if (__builtin_mul_overflow(nmemb, size, &total_size)) {
        if (IS_USED(MODULE_MALLOC_TRACING)) {
            printf("calloc(%u, %u) @0x%" PRIxTXTPTR " overflowed\n",
                   (unsigned)nmemb, (unsigned)size, pc);
        }
        return NULL;
    }

    mutex_lock(&_lock);
    void *res = __real_malloc(total_size);
    mutex_unlock(&_lock);
    if (res) {
        memset(res, 0, total_size);
    }

    if (IS_USED(MODULE_MALLOC_TRACING)) {
        printf("calloc(%u, %u) @0x%" PRIxTXTPTR " returned %p\n",
               (unsigned)nmemb, (unsigned)size, pc, res);
    }

    return res;
}

void * __attribute__((used))__wrap_realloc(void *ptr, size_t size)
{
    uinttxtptr_t pc;
    if (IS_USED(MODULE_MALLOC_TRACING)) {
        pc = cpu_get_caller_pc();
    }

    assert(!irq_is_in());
    mutex_lock(&_lock);
    void *new = __real_realloc(ptr, size);
    mutex_unlock(&_lock);

    if (IS_USED(MODULE_MALLOC_TRACING)) {
        printf("realloc(%p, %u) @0x%" PRIxTXTPTR " returned %p\n",
               ptr, (unsigned)size, pc, new);
    }
    return new;
}

/** @} */
