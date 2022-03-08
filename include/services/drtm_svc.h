/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * DRTM service
 *
 * Authors:
 * 	Lucian Paul-Trifu <lucian.paultrifu@gmail.com>
 * 	Brian Nezvadovitz
 *
 */

#ifndef ARM_DRTM_SVC_H
#define ARM_DRTM_SVC_H

/*
 * SMC function IDs for DRTM Service
 * Upper word bits set: Fast call, SMC64, Standard Secure Svc. Call (OEN = 4)
 */

#define ARM_DRTM_SVC_VERSION		0xC4000110u
#define ARM_DRTM_SVC_FEATURES		0xC4000111u
#define ARM_DRTM_SVC_UNPROTECT_MEM	0xC4000113u
#define ARM_DRTM_SVC_DYNAMIC_LAUNCH	0xC4000114u
#define ARM_DRTM_SVC_CLOSE_LOCALITY	0xC4000115u
#define ARM_DRTM_SVC_GET_ERROR		0xC4000116u
#define ARM_DRTM_SVC_SET_ERROR		0xC4000117u
#define ARM_DRTM_SVC_SET_TCB_HASH	0xC4000118u
#define ARM_DRTM_SVC_LOCK_TCB_HASHES	0xC4000119u

#define ARM_DRTM_FEATURES_TPM		0x1u
#define ARM_DRTM_FEATURES_MEM_REQ	0x2u
#define ARM_DRTM_FEATURES_DMA_PROT	0x3u
#define ARM_DRTM_FEATURES_BOOT_PE_ID	0x4u
#define ARM_DRTM_FEATURES_TCB_HASHES	0x5u

#define is_drtm_fid(_fid) \
	(((_fid) >= ARM_DRTM_SVC_VERSION) && ((_fid) <= ARM_DRTM_SVC_SET_ERROR))

/* ARM DRTM Service Calls version numbers */
#define ARM_DRTM_VERSION_MAJOR	0x0000u
#define ARM_DRTM_VERSION_MINOR	0x0001u
#define ARM_DRTM_VERSION \
	((ARM_DRTM_VERSION_MAJOR << 16) | ARM_DRTM_VERSION_MINOR)

/* Initialization routine for the DRTM service */
int drtm_setup(void);

/* Handler to be called to handle DRTM SMC calls */
uint64_t drtm_smc_handler(uint32_t smc_fid,
		uint64_t x1,
		uint64_t x2,
		uint64_t x3,
		uint64_t x4,
		void *cookie,
		void *handle,
		uint64_t flags);


#endif /* ARM_DRTM_SVC_H */
