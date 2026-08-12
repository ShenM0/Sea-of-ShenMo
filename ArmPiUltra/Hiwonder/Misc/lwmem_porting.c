/**
 * @file lwmem_porting.c
 * @author Lu Yongping (Lucas@hiwonder.com)
 * @brief lwmem 接口移植及内存空间定义
 * @version 0.1
 * @date 2023-06-02
 *
 * @copyright Copyright (c) 2023
 *
 */

#include "lwmem.h"
uint8_t lwmem_ram1[256 * 1];
uint8_t lwmem_ram2[1024 * 6];

lwmem_region_t lwmem_regions[] = {
    { (void*)lwmem_ram1, 256 * 1 }, /* 顺序不能变， ccmram地址比ram地址低，一定要放前面否则无法正常初始化内存*/
    { (void*)lwmem_ram2,  1024 * 6 },
    { NULL, 0}
};
