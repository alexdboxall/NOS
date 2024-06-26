#pragma once

#include <common.h>
#include <bootloader.h>

void DeallocPhys(size_t addr);
void DeallocPhysContiguous(size_t addr, size_t bytes);
size_t AllocPhys(void);
size_t AllocPhysContiguous(size_t bytes, size_t min_addr, size_t max_addr, size_t boundary);
size_t GetTotalPhysKilobytes(void);
size_t GetFreePhysKilobytes(void);

void InitPhys(struct kernel_boot_info* boot_info);
void ReinitPhys(void);

