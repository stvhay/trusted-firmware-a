/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * DRTM resource: TCB hashes.
 *
 */
#ifndef DRTM_RES_TCB_HASHES_H
#define DRTM_RES_TCB_HASHES_H

int drtm_tcb_hashes_init(void);
uint64_t drtm_features_tcb_hashes(void *ctx);
void drtm_dl_ensure_tcb_hashes_are_final(void);
void drtm_serialise_tcb_hashes_table(char *dst,
                         size_t *tcb_hashes_table_size_out);

#endif /* DRTM_RES_TCB_HASHES_H */
