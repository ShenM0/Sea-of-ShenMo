/**
 * @file lwmem_porting.h
 * @author Lu Yongping (Lucas@hiwonder.com)
 * @brief lwmem 接口移植及内存空间定义
 * @version 0.1
 * @date 2023-06-02
 *
 * @copyright Copyright (c) 2023
 *
 */

#ifndef LWMEM_PORTING_H
#define LWMEM_PORTING_H

#include "lwmem.h"

extern lwmem_region_t lwmem_regions[];

#define LWMEM_CCM_REGION (&lwmem_regions[0])
#define LWMEM_RAM_REGION (&lwmem_regions[1])

#define LWMEM_CCM_MALLOC(size)    lwmem_malloc_ex(NULL, LWMEM_CCM_REGION, (size))
#define LWMEM_RAM_MALLOC(size)    lwmem_malloc_ex(NULL, LWMEM_RAM_REGION, (size))
#define LWMEM_FREE(ptr)           lwmem_free_ex(NULL, (ptr))


#endif


