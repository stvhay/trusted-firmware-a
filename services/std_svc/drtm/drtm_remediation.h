/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 */
#ifndef DRTM_REMEDIATION_H
#define DRTM_REMEDIATION_H

uintptr_t drtm_set_error(uint64_t x1, void *ctx);
uintptr_t drtm_get_error(void *ctx);

void drtm_enter_remediation(long long error_code, const char *error_str);

#endif /* DRTM_REMEDIATION_H */
