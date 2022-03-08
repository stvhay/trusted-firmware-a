/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * DRTM measurements into TPM PCRs.
 *
 * Authors:
 *	Lucian Paul-Trifu <lucian.paultrifu@gmail.com>
 *
 */
#include <assert.h>

#include <mbedtls/md.h>

#include <common/debug.h>
#include <drivers/auth/mbedtls/mbedtls_common.h>
#include <lib/xlat_tables/xlat_tables_v2.h>

#include "drtm_main.h"
#include "drtm_measurements.h"

#define XLAT_PAGE_SIZE        PAGE_SIZE
#if XLAT_PAGE_SIZE != DRTM_PAGE_SIZE
#warning "xlat library page size differs from DRTM page size;"\
         " mmap_add_dynamic_region() calls to the xlat library might fail"
#endif


#define DRTM_EVENT_ARM_BASE          0x9000U
#define DRTM_EVENT_TYPE(n)           (DRTM_EVENT_ARM_BASE + (unsigned int)(n))

#define DRTM_EVENT_ARM_PCR_SCHEMA        DRTM_EVENT_TYPE(1)
#define DRTM_EVENT_ARM_DCE               DRTM_EVENT_TYPE(2)
#define DRTM_EVENT_ARM_DCE_PUBKEY        DRTM_EVENT_TYPE(3)
#define DRTM_EVENT_ARM_DLME              DRTM_EVENT_TYPE(4)
#define DRTM_EVENT_ARM_DLME_EP           DRTM_EVENT_TYPE(5)
#define DRTM_EVENT_ARM_DEBUG_CONFIG      DRTM_EVENT_TYPE(6)
#define DRTM_EVENT_ARM_NONSECURE_CONFIG  DRTM_EVENT_TYPE(7)
#define DRTM_EVENT_ARM_DCE_SECONDARY     DRTM_EVENT_TYPE(8)
#define DRTM_EVENT_ARM_TZFW              DRTM_EVENT_TYPE(9)
#define DRTM_EVENT_ARM_SEPARATOR         DRTM_EVENT_TYPE(10)

#define DRTM_NULL_DATA  ((unsigned char []){ 0 })
#define DRTM_EVENT_ARM_SEP_DATA \
	(const unsigned char []){'A', 'R', 'M', '_', 'D', 'R', 'T', 'M' }

#if !defined(DRTM_TPM_HASH_ALG)
/*
 * This is an error condition.  However, avoid emitting a further error message,
 * since an explanatory one will have already been emitted by the header file.
 */
#define DRTM_TPM_HASH_ALG  TPM_ALG_NONE
#define DRTM_MBEDTLS_HASH_ALG  MBEDTLS_MD_NONE
#else
#define DRTM_MBEDTLS_HASH_ALG \
	EXPAND_AND_COMBINE(MBEDTLS_MD_SHA, DRTM_SHA_ALG)
#endif


#define CHECK_RC(rc, func_call) { \
	if ((rc)) { \
		ERROR("%s(): " #func_call "failed unexpectedly rc=%d\n",  \
		      __func__, rc);  \
		panic();  \
	}  \
}


int drtm_measurements_init(void)
{
	mbedtls_init();

	return 0;
}

#define calc_hash(data_ptr, data_len, output) \
	mbedtls_md(mbedtls_md_info_from_type((mbedtls_md_type_t)DRTM_MBEDTLS_HASH_ALG),\
	           data_ptr, data_len, output)

enum drtm_retc drtm_take_measurements(const struct_drtm_dl_args *a,
                                      struct drtm_event_log *log)
{
	struct tpm_log_1digest_shaX {
		struct tpm_log_digests digests_1;
		struct tpm_log_digest d;
		unsigned char digest[MBEDTLS_MD_MAX_SIZE];
	} __packed __aligned(__alignof(struct tpm_log_digests));
	struct tpm_log_1digest_shaX digests_buf = {
		.digests_1 = {
			.count = 1,
		},
		.d = (struct tpm_log_digest) {
			.h_alg = DRTM_TPM_HASH_ALG,
			.buf_bytes = sizeof(((struct tpm_log_1digest_shaX *)0)->digest),
		},
		{0}
	};
	int rc;
	uint8_t pcr_schema;
	tpm_log_info_t *const tpm_log_info = &log->tpm_log_info;

	rc = tpm_log_init(log->tpm_log_mem, sizeof(log->tpm_log_mem),
	              (enum tpm_hash_alg[]){ DRTM_TPM_HASH_ALG }, 1,
	              tpm_log_info);
	CHECK_RC(rc, tpm_log_init);

	/**
	 * Measurements extended into PCR-17.
	 *
	 * PCR-17: Measure the DCE image.  Extend digest of (char)0 into PCR-17
	 * since the D-CRTM and the DCE are not separate.
	 */
	rc = calc_hash(DRTM_NULL_DATA, sizeof(DRTM_NULL_DATA), digests_buf.digest);
	CHECK_RC(rc, calc_hash(NULL_DATA_1));

	rc = tpm_log_add_event(tpm_log_info, DRTM_EVENT_ARM_DCE, TPM_PCR_17,
	               &digests_buf.digests_1, NULL, 0);
	CHECK_RC(rc, tpm_log_add_event_arm_dce);

	/* PCR-17: Measure the PCR schema DRTM launch argument. */
	pcr_schema = DL_ARGS_GET_PCR_SCHEMA(a);
	rc = calc_hash(&pcr_schema, sizeof(pcr_schema), digests_buf.digest);
	CHECK_RC(rc, calc_hash(pcr_schema));

	rc = tpm_log_add_event(tpm_log_info, DRTM_EVENT_ARM_PCR_SCHEMA, TPM_PCR_17,
	               &digests_buf.digests_1, NULL, 0);
	CHECK_RC(rc, tpm_log_add_event(ARM_PCR_SCHEMA_17));

	/* PCR-17: Measure the enable state of external-debug, and trace. */
	/*
	 * TODO: Measure the enable state of external-debug and trace.  This should
	 * be returned through a platform-specific hook.
	 */

	/* PCR-17: Measure the security lifecycle state. */
	/*
	 * TODO: Measure the security lifecycle state.  This is an implementation-
	 * defined value, retrieved through an implementation-defined mechanisms.
	 */

	/*
	 * PCR-17: Optionally measure the NWd DCE.
	 * It is expected that such subsequent DCE stages are signed and verified.
	 * Whether they are measured in addition to signing is implementation
	 * -defined.
	 * Here the choice is to not measure any NWd DCE, in favour of PCR value
	 * resilience to any NWd DCE updates.
	 */

	/* PCR-17: End of DCE measurements. */
	rc = calc_hash(DRTM_EVENT_ARM_SEP_DATA, sizeof(DRTM_EVENT_ARM_SEP_DATA),
	               digests_buf.digest);
	CHECK_RC(rc, calc_hash(ARM_SEP_DATA_17));

	rc = tpm_log_add_event(tpm_log_info, DRTM_EVENT_ARM_SEPARATOR, TPM_PCR_17,
	               &digests_buf.digests_1,
	               DRTM_EVENT_ARM_SEP_DATA, sizeof(DRTM_EVENT_ARM_SEP_DATA));
	CHECK_RC(rc, tpm_log_add_event(ARM_SEPARATOR_17));

	/**
	 * Measurements extended into PCR-18.
	 *
	 * PCR-18: Measure the PCR schema DRTM launch argument.
	 */
	pcr_schema = DL_ARGS_GET_PCR_SCHEMA(a);
	rc = calc_hash(&pcr_schema, sizeof(pcr_schema), digests_buf.digest);
	CHECK_RC(rc, calc_hash(pcr_schema));

	rc = tpm_log_add_event(tpm_log_info, DRTM_EVENT_ARM_PCR_SCHEMA, TPM_PCR_18,
	               &digests_buf.digests_1, NULL, 0);
	CHECK_RC(rc, tpm_log_add_event(ARM_PCR_SCHEMA_17));

	/*
	 * PCR-18: Measure the public key used to verify DCE image(s) signatures.
	 * Extend digest of (char)0, since we do not expect the NWd DCE to be
	 * present.
	 */
	assert(a->dce_nwd_size == 0);
	rc = calc_hash(DRTM_NULL_DATA, sizeof(DRTM_NULL_DATA), digests_buf.digest);
	CHECK_RC(rc, calc_hash(NULL_DATA_2));

	rc = tpm_log_add_event(tpm_log_info, DRTM_EVENT_ARM_DCE_PUBKEY, TPM_PCR_18,
	               &digests_buf.digests_1, NULL, 0);
	CHECK_RC(rc, tpm_log_add_event(ARM_DCE_PUBKEY));

	/* PCR-18: Measure the DLME image. */
	uintptr_t dlme_img_mapping;
	size_t dlme_img_mapping_bytes;

	dlme_img_mapping_bytes = ALIGNED_UP(a->dlme_img_size, DRTM_PAGE_SIZE);
	rc = mmap_add_dynamic_region_alloc_va(a->dlme_paddr + a->dlme_img_off,
	                         &dlme_img_mapping,
	                         dlme_img_mapping_bytes, MT_RO_DATA | MT_NS);
	if (rc) {
		WARN("DRTM: %s: mmap_add_dynamic_region() failed rc=%d\n", __func__, rc);
		return INTERNAL_ERROR;
	}

	rc = calc_hash((void *)dlme_img_mapping, a->dlme_img_size,
	               digests_buf.digest);
	CHECK_RC(rc, calc_hash(dlme_img));

	rc = tpm_log_add_event(tpm_log_info, DRTM_EVENT_ARM_DLME, TPM_PCR_18,
	               &digests_buf.digests_1, NULL, 0);
	CHECK_RC(rc, tpm_log_add_event(ARM_DLME));

	rc = mmap_remove_dynamic_region(dlme_img_mapping, dlme_img_mapping_bytes);
	CHECK_RC(rc, mmap_remove_dynamic_region);

	/* PCR-18: Measure the DLME image entry point. */
	uint64_t dlme_img_ep = DL_ARGS_GET_DLME_ENTRY_POINT(a);

	rc = calc_hash((unsigned char *)&dlme_img_ep, sizeof(dlme_img_ep),
	               digests_buf.digest);
	CHECK_RC(rc, calc_hash(dlme_img_ep_off));

	rc = tpm_log_add_event(tpm_log_info, DRTM_EVENT_ARM_DLME_EP, TPM_PCR_18,
	               &digests_buf.digests_1, NULL, 0);
	CHECK_RC(rc, tpm_log_add_event(ARM_DLME_EP));

	/* PCR-18: End of DCE measurements. */
	rc = calc_hash(DRTM_EVENT_ARM_SEP_DATA, sizeof(DRTM_EVENT_ARM_SEP_DATA),
	               digests_buf.digest);
	CHECK_RC(rc, calc_hash(ARM_SEP_DATA_18));

	rc = tpm_log_add_event(tpm_log_info, DRTM_EVENT_ARM_SEPARATOR, TPM_PCR_18,
	               &digests_buf.digests_1,
	               DRTM_EVENT_ARM_SEP_DATA, sizeof(DRTM_EVENT_ARM_SEP_DATA));
	CHECK_RC(rc, tpm_log_add_event(ARM_SEPARATOR_18));

	/*
	 * If the DCE is unable to log a measurement because there is no available
	 * space in the event log region, the DCE must extend a hash of the value
	 * 0xFF (1 byte in size) into PCR[17] and PCR[18] and enter remediation.
	 */
	return SUCCESS;
}

void drtm_serialise_event_log(char *dst, const struct drtm_event_log *src,
                              size_t *event_log_size_out)
{
	if (src) {
		tpm_log_serialise(dst, &src->tpm_log_info, event_log_size_out);
	} else {
		if (dst != NULL) {
			ERROR("%s(): cannot serialise the unexpected NULL event log\n",
			       __func__);
			panic();
		}
		if (event_log_size_out) {
			/*
			 * DRTM Beta0: Note that the advertised minimum required size ought
			 * to be 64KiB, rather than a more economical size of our choosing.
			 */
			*event_log_size_out = DRTM_EVENT_LOG_INIT_SIZE;
		}
	}
}
