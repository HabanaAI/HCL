#pragma once

#define DISABLE_AVX_MODE _POWER_PC_

// align to size, size should be power of 2
// macro aligned to LKD implementation
#define ALIGN_UP(addr, size) (((addr) + (size) - 1) & ~((size) - 1))
