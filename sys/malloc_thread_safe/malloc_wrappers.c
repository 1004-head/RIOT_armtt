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
static unsigned int PERIPHLIMIT = 0xE00FFFFF;

unsigned int HEAPSIZE = 0x3B600;

size_t SIZE_TABLE[7] = {0x20, 0x100, 0x1000, 0x4000, 0x7FFFF, 0x1FFF, 0xA00FFFFF};

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
  void *baseptr;
  void *endptr;
};
typedef struct zerofat_regioninfo_s *zerofat_regioninfo_t;

struct zerofat_regioninfo_s ZEROFAT_REGION_INFO[4];

/*
 * initialize mpu region
 */
void init_MPU(){
  setMPU(4UL, ROMADDRESS, ROMLIMIT, ARM_MPU_RO, ARM_MPU_EXEC); //ROM set region 5
  setMPU(5UL, DATAADDRESS, DATALIMIT, ARM_MPU_RW, ARM_MPU_XN); //data, bss set region 6
  setMPU(6UL, STACKADDRESS, STACKLIMIT, ARM_MPU_RW, ARM_MPU_XN); //stack set region 7
  setMPU(7UL, PERIPHADDRESS, PERIPHLIMIT, ARM_MPU_RW, ARM_MPU_XN); //peripheral set region 8
}

/*
 * zerofat initialize mpu region, data structure
 */
bool zerofat_init(void)
{
  init_MPU();
  printf("\nMPU Initializing is done\n\n");
  for(uint8_t i=0; i<4; i++)
  {
    uint8_t *heapptr = 0x20003000 + (HEAPSIZE/4)*i; //zerofat heep ptr
    uint8_t *startptr = 0x20003000 + (HEAPSIZE/2)*((i+1)/2); //zerofat region start ptr
    zerofat_regioninfo_t info = ZEROFAT_REGION_INFO + i; //zerofat region info
    info->freelist = NULL;
    //info->freeptr = startptr; //zerofat can allocate ptr
    info->baseptr = startptr; //zerofat region start ptr
    if(i%2==0)
    {
      info->freeptr = startptr;
      info->endptr = startptr + HEAPSIZE/4; //zerofat region end ptr == slider
    }
    else
    {
      info->freeptr = startptr - SIZE_TABLE[i];
      info->endptr = startptr - HEAPSIZE/4;
    }
    setMPU(i, heapptr, heapptr+HEAPSIZE/4-1, ARM_MPU_RW, ARM_MPU_XN); //heap set region 1, 2, 3, 4
    printf("idx-%d: freeptr-%p, baseptr-%p, endptr-%p\n", i, info->freeptr, info->baseptr, info->endptr);
  }
  zerofat_malloc_inited = true;
  printf("Zerofat Initializing is done\n\n");

  return true;
}

/*
 * get real allocation size in zerofat
 */
uint8_t get_alloc_idx(size_t size)
{
  if(SIZE_TABLE[0] >= size) return 0;
  else if(SIZE_TABLE[1] >= size) return 1;
  else if (SIZE_TABLE[2] >= size) return 2;
  else return 3;
}

/*
 * get smallest power of 2
 */
size_t get_smallest_po2(size_t size)
{
  if(size <= 0) return 1;
  size_t origin = size;

  size_t msb = 0;
  while(size > 0){
    size >>= 1;
    msb++;
  }

  if((1 << (msb - 1)) == origin){
    return origin;
  }

  return 1 << msb;
}

/*
 * zerofat allocator malloc
 */
void __attribute__((used)) *__wrap_malloc(size_t size)
{
    if(!zerofat_malloc_inited)
      zerofat_init();

    uinttxtptr_t pc;
    if(IS_USED(MODULE_MALLOC_TRACING)){
      pc = cpu_get_calloer_pc();
    }
    assert(!irq_is_in());

    /*
     * 자 만들어야 하는걸 보자고
     * 1. size가 들어오면 적절한 2의 지수승 사이즈에 매핑
     * 2. 결정된 사이즈를 사이즈 테이블에 저장
     * 3. 만약 사이즈 테이블이 채워져 있으면 사이즈테이블에서 적절한 사이즈로 설정
     * 4. 사이즈 테이블에 있는 최대 크기보다 크게 들어오면 테이블값 재조정 및 영역 재할당
     * 5. 영역이 다 차게 되면 인접한 영역의 프리리스트를 가져와서 영역 확장
     */

    size_t smallest_po2 = get_smallest_po2(size);
    printf("smallest po2 value is %d\n", smallest_po2);
    
    uint8_t idx = get_alloc_idx(size); //zerofat allocate region index
    size_t alloc_size = SIZE_TABLE[idx]; //zerofat allocate region size
    zerofat_regioninfo_t info = &ZEROFAT_REGION_INFO[idx]; //zerofat region info
    printf("zerofat region idx : %d, alloc size : %d\n", idx, alloc_size);
    void *ptr;

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
    void *freeptr;
    if(idx%2 == 0)
    {
      freeptr = (uint8_t *)ptr + alloc_size;
      if(freeptr > info->endptr){
        size_t expand_size = 0x800;
        zerofat_regioninfo_t neighbor_info = &ZEROFAT_REGION_INFO[idx+1];
        void *endptr = info->endptr + expand_size;
        info->endptr = endptr;
        neighbor_info->endptr = endptr;
        setMPU(idx, info->baseptr, info->endptr-1, ARM_MPU_RW, ARM_MPU_XN);
        setMPU(idx+1, neighbor_info->endptr, neighbor_info->baseptr-1, ARM_MPU_RW, ARM_MPU_XN);
      }
    }
    else
    {
      freeptr = (uint8_t *)ptr - alloc_size;
      if(freeptr < info->endptr)
      {
        size_t expand_size = 0x800;
        zerofat_regioninfo_t neighbor_info = &ZEROFAT_REGION_INFO[idx-1];
        void *endptr = info->endptr - expand_size;
        info->endptr = endptr;
        neighbor_info->endptr = endptr;
        setMPU(idx, info->endptr, info->baseptr-1, ARM_MPU_RW, ARM_MPU_XN);
        setMPU(idx-1, neighbor_info->baseptr, neighbor_info->endptr-1, ARM_MPU_RW, ARM_MPU_XN);
      }
    }
    info->freeptr = freeptr;

    printf("zerofat ptr is %p\n\n", ptr);
    mutex_unlock(&_lock);
    if (IS_USED(MODULE_MALLOC_TRACING)) {
        printf("malloc(%u) @ 0x%" PRIxTXTPTR " returned %p\n",
               (unsigned)size, pc, ptr);
    }
    return ptr;
}

uint8_t get_free_idx(void *ptr)
{
  bool legacy_check = false;
  for(int i=0; i<4; i++){
    zerofat_regioninfo_t info = &ZEROFAT_REGION_INFO[i];
    void *base = info->baseptr;
    void *end = info->endptr;
    if(i%2==0)
    {
      if(ptr >= base && ptr <= end) return i;
      else legacy_check = true;
    }else{
      if(ptr >= end && ptr <= base) return i;
      else legacy_check = true;
    }
  }

  if(legacy_check) return 9;
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

    uint8_t idx = get_free_idx(ptr);
    printf("idx of ptr %p = %d\n", ptr, idx);

    if (IS_USED(MODULE_MALLOC_TRACING)) {
        uinttxtptr_t pc = cpu_get_caller_pc();
        printf("free(%p) @0x%" PRIxTXTPTR ")\n", ptr, pc);
    }
    assert(!irq_is_in());
    mutex_lock(&_lock);

    if(idx == 9)
    {
      __real_free(ptr);
      mutex_unlock(&_lock);
      return;
    }

    zerofat_regioninfo_t info = &ZEROFAT_REGION_INFO[idx];
    zerofat_freelist_t newfreelist = (zerofat_freelist_t)ptr;
    zerofat_freelist_t oldfreelist = info->freelist;
    printf("new freelist : %p, oldfreelist : %p\n", newfreelist, oldfreelist);

    newfreelist->next = oldfreelist;
    info->freelist = newfreelist;
    printf("freelist ptr : %p\n", info->freelist);
    mutex_unlock(&_lock);

    printf("zerofat ptr free is done\n\n");
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
