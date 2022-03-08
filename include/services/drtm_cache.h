/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 */

#ifndef __DRTM_CACHE_H
#define __DRTM_CACHE_H

#include <stdbool.h>

/*
 * XXX Note: the generic protected DRTM resources are being specialised into
 * DRTM TCB hashes.  Platform resources retrieved through the generic DRTM cache
 * are going to be retrieved through bespoke interfaces instead.
 * This file will be removed once the transition is complete.
 */

void drtm_cache_init(void);

int drtm_cache_resource_opt(const char *id, size_t bytes, const char *data, bool cache_data);
#define drtm_cache_resource(id, bytes, data) \
        drtm_cache_resource_opt(id, bytes, data, true)
#define drtm_cache_resource_ptr(id, bytes, data) \
        drtm_cache_resource_opt(id, bytes, data, false)

void drtm_cache_get_resource(const char *id,
                             const char **res_out, size_t *res_out_bytes);

#endif   /* __DRTM_CACHE_H */
