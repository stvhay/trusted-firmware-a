/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 */
#ifndef DRTM_CACHE_H
#define DRTM_CACHE_H

#pragma pack(push, 1)
struct cached_res {
	char id[32];
	size_t bytes;
	const char *data_ptr;  /* If NULL, then the data follows. */
	const char data[];
};
#pragma pack(pop)

#endif   /* DRTM_CACHE_H */
