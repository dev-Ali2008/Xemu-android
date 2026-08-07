// SPDX-License-Identifier: BSD-2-Clause
// Copyright © 2021 Billy Laws

#pragma once

#include <stdint.h>

enum {
    ADRENOTOOLS_DRIVER_CUSTOM = 1 << 0,
    ADRENOTOOLS_DRIVER_FILE_REDIRECT = 1 << 1,
    ADRENOTOOLS_DRIVER_GPU_MAPPING_IMPORT = 1 << 2,
};

#define ADRENOTOOLS_GPU_MAPPING_SUCCEEDED_MAGIC 0xDEADBEEF

struct adrenotools_gpu_mapping {
    void *host_ptr;
    uint64_t gpu_addr;
    uint64_t size;
    uint64_t flags;
};
