/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 */
#ifndef DRTM_MEASUREMENTS_H
#define DRTM_MEASUREMENTS_H

#include <stdint.h>

#include <lib/tpm/tpm_log.h>

#include "drtm_main.h"

#define DRTM_EVENT_LOG_INIT_SIZE     ((size_t)(768))

#if !defined(DRTM_SHA_ALG)
#error "The DRTM service requires definition of the DRTM_SHA_ALG macro"
#else
#define COMBINE(a, b) a ## b
#define EXPAND_AND_COMBINE(a, b) COMBINE(a, b)
#define DRTM_TPM_HASH_ALG EXPAND_AND_COMBINE(TPM_ALG_SHA, DRTM_SHA_ALG)

#if DRTM_SHA_ALG == 256
#define DRTM_TPM_HASH_ALG_DSIZE  32
#elif DRTM_SHA_ALG == 384
#define DRTM_TPM_HASH_ALG_DSIZE  48
#elif DRTM_SHA_ALG == 512
#define DRTM_TPM_HASH_ALG_DSIZE  64
#endif

#endif


struct drtm_event_log {
	tpm_log_info_t tpm_log_info;
	uint32_t tpm_log_mem[DRTM_EVENT_LOG_INIT_SIZE / sizeof(uint32_t)];
};
/* Opaque / encapsulated type. */
typedef struct drtm_event_log drtm_event_log_t;


int drtm_measurements_init(void);
enum drtm_retc drtm_take_measurements(const struct_drtm_dl_args *a,
                              drtm_event_log_t *log);
void drtm_serialise_event_log(char *dst, const drtm_event_log_t *src_log,
                              size_t *event_log_size_out);

#endif /* DRTM_MEASUREMENTS_H */
