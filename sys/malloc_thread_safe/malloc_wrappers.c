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

size_t SIZE_TABLE[7] = {0, 0, 0, 0, 0x7FFFF, 0x1FFF, 0xA00FFFFF};

/*
 * zerofat data structure
 */
struct zerofat_freelist_s
{
  uintptr_t _reserved;
  struct zerofat_freelist_s *next;
};
typedef struct zerofat_freelist_s *zerofat_freelist_t;

struct zerofat_alloclist_s
{
  uintptr_t _reserved;
  struct zerofat_alloclist_s *next;
};
typedef struct zerofat_alloclist_s *zerofat_alloclist_t;

struct zerofat_regioninfo_s
{
  zerofat_alloclist_t alloclist;
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
    SIZE_TABLE[i] = 0;
    printf("%d\n", i);
    void *heapptr = 0x20003000 +((HEAPSIZE/4)*i); //zerofat heep ptr
    zerofat_regioninfo_t info = ZEROFAT_REGION_INFO + i; //zerofat region info
    info->alloclist = NULL;
    info->freelist = NULL;
    info->baseptr = heapptr; //zerofat region start baseptr
    info->freeptr = heapptr;
    info->endptr = heapptr + (HEAPSIZE/4) - 1; //zerofat region end ptr == slider
    setMPU(i, info->baseptr, info->endptr, ARM_MPU_RW, ARM_MPU_XN); //heap set region 1, 2, 3, 4
    printf("%p - %p = %p\n", info->endptr, info->baseptr, info->endptr - info->baseptr);
    printf("idx-%d: freeptr-%p, baseptr-%p, endptr-%p\n", i, info->freeptr, info->baseptr, info->endptr);
  }
  zerofat_malloc_inited = true;
  printf("Zerofat Initializing is done\n\n");

  return true;
}

/*
 * modify size table and region info
 */
void modify_region(size_t change_size)
{
  size_t tmp = 0;
  for(int i=3; i>0; i--){
    tmp = SIZE_TABLE[i];
    SIZE_TABLE[i] = change_size;
    change_size = tmp;

    zerofat_regioninfo_t newregion = &ZEROFAT_REGION_INFO[i];
    zerofat_alloclist_t allocnode = newregion->alloclist;
    while(allocnode != NULL){
      realloc(allocnode, SIZE_TABLE[i]);
    }
  }
}

/*
 * get real allocation size in zerofat
 */
uint8_t get_alloc_idx(size_t size)
{
  for(int i = 0; i<4; i++){
    if(SIZE_TABLE[i] == 0){
      SIZE_TABLE[i] = size;
      return i;
    }
  }

  if(SIZE_TABLE[0] >= size) return 0;
  else if(SIZE_TABLE[1] >= size) return 1;
  else if(SIZE_TABLE[2] >= size) return 2;
  else if(SIZE_TABLE[3] >= size) return 3;
  else{
    modify_region(size);
    for(int i=0; i<4; i++) printf("size table idx %d = %d\n", i, SIZE_TABLE[i]);
    return 3;
  }
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
     * 1. size가 들어오면 적절한 2의 지수승 사이즈에 매핑 "해결"
     * 2. 결정된 사이즈를 사이즈 테이블에 저장 "해결"
     * 3. 만약 사이즈 테이블이 채워져 있으면 사이즈테이블에서 적절한 사이즈로 설정 "해결"
     * 4. 사이즈 테이블에 있는 최대 크기보다 크게 들어오면 테이블값 재조정 및 영역 재할당 "해결"
     * 5. 영역이 다 차게 되면 인접한 영역의 프리리스트를 가져와서 영역 확장 - TODO
     * 6. 할당 방식과 메모리 구조 변경 (슬라이더 제거, 순차적으로 할당 등) "해결"
     * 7. 초기화 코드 수정"해결"
     * 8. 사이즈테이블 변경 후 해당 사이즈에 맞게 포인터 재할당 - TODO
     * 9. realloc 함수 작성 - "해결"
     */

    size_t po2_size = get_smallest_po2(size);
    printf("smallest po2 value is %d\n", po2_size);    

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
    freeptr = (uint8_t *)ptr + alloc_size;
    if(freeptr > info->endptr){
      
      zerofat_regioninfo_t neighbor_info = &ZEROFAT_REGION_INFO[idx+1];
      zerofat_freelist_t neighbor_freelist = neighbor_info->freelist;

      void *endptr = NULL;
      if(neighbor_freelist == NULL) endptr = neighbor_info->baseptr + SIZE_TABLE[idx+1] - 1;
      // 여기서 이제 기존에 idx+1 region에 할당되어 있던 포인터들을 다 한 칸씩 밀어서 옮겨줘야 함
      else if(neighbor_freelist+1 == NULL) endptr = (void *) &neighbor_freelist[0] + SIZE_TABLE[idx+1] - 1;
      else endptr = (void *)&neighbor_freelist[1] - 1;
      neighbor_info->freelist = neighbor_freelist->next;

      info->endptr = endptr;
      neighbor_info->baseptr = endptr+1;
      setMPU(idx, info->baseptr, info->endptr, ARM_MPU_RW, ARM_MPU_XN);
      setMPU(idx+1, neighbor_info->baseptr, neighbor_info->endptr, ARM_MPU_RW, ARM_MPU_XN);
    }
    info->freeptr = freeptr;

    zerofat_alloclist_t newalloclist = (zerofat_alloclist_t) ptr;
    zerofat_alloclist_t oldalloclist = info->alloclist;
    newalloclist->next = oldalloclist;
    info->alloclist = newalloclist;
    printf("newalloclist : %p, oldalloclist : %p\n", newalloclist, oldalloclist);

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
  for(int i=0; i<4; i++){
    zerofat_regioninfo_t info = &ZEROFAT_REGION_INFO[i];
    void *base = info->baseptr;
    void *end = info->endptr;
    if(ptr >= base && ptr <= end) return i;
  }
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

    if(ptr == NULL || size == 0) return __wrap_malloc(size);

    void *newptr = __wrap_malloc(size);
    if(newptr == NULL) return NULL;

    size_t cpy_size;
    size_t idx = get_alloc_idx(ptr);
    size_t ptr_size = SIZE_TABLE[idx];
    memcpy(newptr, ptr, cpy_size);
    __wrap_free(ptr);
    mutex_unlock(&_lock);

    if (IS_USED(MODULE_MALLOC_TRACING)) {
        printf("realloc(%p, %u) @0x%" PRIxTXTPTR " returned %p\n",
               ptr, (unsigned)size, pc, newptr);
    }
    return newptr;
}

/** @} */
