// SPDX-License-Identifier: BSD-2-Clause
// Copyright © 2021 Billy Laws

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "priv.h"

void *adrenotools_open_libvulkan(int dlopenMode, int featureFlags, const char *tmpLibDir, const char *hookLibDir, const char *customDriverDir, const char *customDriverName, const char *fileRedirectDir, void **userMappingHandle);
bool adrenotools_import_user_mem(void *handle, void *hostPtr, uint64_t size);
bool adrenotools_mem_gpu_allocate(void *handle, uint64_t *size);
bool adrenotools_mem_cpu_map(void *handle, void *hostPtr, uint64_t size);
bool adrenotools_validate_gpu_mapping(void *handle);
void adrenotools_set_turbo(bool turbo);

#ifdef __cplusplus
}
#endif
