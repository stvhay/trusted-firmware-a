/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * DRTM protected-resources cache
 *
 * Authors:
 *	Lucian Paul-Trifu <lucian.paultrifu@gmail.com>
 */

#include <common/debug.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <services/drtm_cache.h>

/*
 * XXX Note: the generic protected DRTM resources are being specialised into
 * DRTM TCB hashes.  Platform resources retrieved through the generic DRTM cache
 * are going to be retrieved through bespoke interfaces instead.
 * This file and drtm_qemu_virt_cached_resources_init.c will be removed once the
 * transition is complete.
 */

static char cache[1 * 1024];
static char *cache_free = cache;
#define CACHE_END ((char *)cache + sizeof(cache))

#include "drtm_qemu_virt_cached_resources_init.c"


static struct cached_res *cache_alloc(size_t bytes)
{
	struct cached_res *r;

	if (cache_free + bytes >= CACHE_END) {
		return NULL;
	}

	r = (struct cached_res *)cache_free;
	cache_free += bytes;

	return r;
}


void drtm_cache_init(void)
{
	const struct cached_res *r;

	memset(&cache, 0, sizeof(cache));

	r = CACHED_RESOURCES_INIT;
	while (r < CACHED_RESOURCES_INIT_END) {
		int rc;

		if (r->data_ptr) {
			rc = drtm_cache_resource_ptr(r->id, r->bytes, r->data_ptr);
		} else {
			rc = drtm_cache_resource(r->id, r->bytes, r->data);
		}
		if (rc) {
			WARN("%s: drtm_cache_resource_opt() failed rc=%d\n", __func__, rc);
			break;
		}

		r = (struct cached_res *)((char *)r + sizeof(*r)
		                          + (r->data_ptr ? 0 : r->bytes));
	}
}

int drtm_cache_resource_opt(const char *id, size_t bytes, const char *data,
                            bool copy_the_data)
{
	struct cached_res *res;
	size_t bytes_req = sizeof(struct cached_res) + (copy_the_data ? bytes : 0);

	if (strnlen(id, sizeof(res->id)) == sizeof(res->id) || !data) {
		return -EINVAL;
	}

	res = cache_alloc(bytes_req);
	if (!res) {
		return -ENOMEM;
	}

	(void)strlcpy(res->id, id, sizeof(res->id));

	res->bytes = bytes;
	if (copy_the_data) {
		res->data_ptr = NULL;
		(void)memcpy((char *)res->data, data, bytes);
	} else {
		res->data_ptr = data;
	}

	return 0;
}

void drtm_cache_get_resource(const char *id,
                             const char **res_out, size_t *res_out_bytes)
{
	struct cached_res *r = (struct cached_res *)cache;

	while ((char *)r < CACHE_END) {
		if (strncmp(r->id, id, sizeof(r->id)) == 0) {
			*res_out = r->data_ptr ? r->data_ptr : r->data;
			*res_out_bytes = r->bytes;
			return;
		}
		r = (struct cached_res *)((char *)r + sizeof(*r)
		                        + (r->data_ptr ? 0 : r->bytes));
	}

	*res_out = NULL;
	*res_out_bytes = 0;
}
