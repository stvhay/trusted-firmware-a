/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * DRTM service
 *
 * Authors:
 *	Lucian Paul-Trifu <lucian.paultrifu@gmail.com>
 * 	Brian Nezvadovitz
 */

#include <stdint.h>

#include <common/debug.h>
#include <common/runtime_svc.h>
#include <lib/xlat_tables/xlat_tables_v2.h>
#include <lib/el3_runtime/context_mgmt.h>
#include <plat/arm/common/plat_arm.h>
#include <plat/common/platform.h>
#include <services/drtm_svc.h>
#include <services/drtm_cache.h>
#include <tools_share/uuid.h>

#include "drtm_dma_prot.h"
#include "drtm_main.h"
#include "drtm_measurements.h"
#include "drtm_remediation.h"
#include "drtm_res_tcb_hashes.h"

#define XLAT_PAGE_SIZE        PAGE_SIZE
#if XLAT_PAGE_SIZE != DRTM_PAGE_SIZE
#warning "xlat library page size differs from DRTM page size;"\
         " mmap_add_dynamic_region() calls to the xlat library might fail"
#endif


enum drtm_dlme_el {
	DLME_AT_EL1,
	DLME_AT_EL2
};
static enum drtm_dlme_el drtm_dlme_el(unsigned int el)
{
	return (enum drtm_dlme_el)el - 1;
}

struct __packed dlme_data_header_v1 {
	uint16_t version;        /* Must be 1. */
	uint16_t this_hdr_size;
	uint8_t __res[4];
	uint64_t dlme_data_size;
	uint64_t dlme_prot_regions_size;
	uint64_t dlme_addr_map_size;
	uint64_t dlme_tpm_log_size;
	uint64_t dlme_tcb_hashes_table_size;
	uint64_t dlme_impdef_region_size;
} __aligned(__alignof(uint16_t /* First member's type, `uint16_t version'. */));

typedef struct dlme_data_header_v1 struct_dlme_data_header;


static uint64_t boot_pe_aff_value;
static int locality2, locality3;


static unsigned int get_highest_ns_el_implemented(void)
{
	return nonsecure_el_implemented(2) != EL_IMPL_NONE ? 2 : 1;
}


int drtm_setup(void)
{
	int rc;

	INFO("++ DRTM service setup\n");

	boot_pe_aff_value = read_mpidr_el1() & MPIDR_AFFINITY_MASK;

	if ((rc = drtm_dma_prot_init())) {
		return rc;
	}

	if ((rc = drtm_tcb_hashes_init())) {
		return rc;
	}

	drtm_cache_init();

	if ((rc = drtm_measurements_init())) {
		return rc;
	}

	return 0;
}


static enum drtm_retc drtm_dl_check_caller_el(void *ctx)
{
	uint64_t spsr_el3 = read_ctx_reg(get_el3state_ctx(ctx), CTX_SPSR_EL3);
	uint64_t dl_caller_el;
	uint64_t dl_caller_aarch;

	dl_caller_el = spsr_el3 >> MODE_EL_SHIFT & MODE_EL_MASK;
	dl_caller_aarch = spsr_el3 >> MODE_RW_SHIFT & MODE_RW_MASK;

	if (dl_caller_el == MODE_EL3) {
		ERROR("DRTM: invalid launch from EL3\n");
		return DENIED;
	}

	if (dl_caller_aarch != MODE_RW_64) {
		ERROR("DRTM: invalid launch from non-AArch64 execution state\n");
		return DENIED;
	}

	return SUCCESS;
}

static enum drtm_retc drtm_dl_check_cores(void)
{
	unsigned int core_not_off;
	uint64_t this_pe_aff_value = read_mpidr_el1() & MPIDR_AFFINITY_MASK;

	if (this_pe_aff_value != boot_pe_aff_value) {
		ERROR("DRTM: invalid launch on a non-boot PE\n");
		return DENIED;
	}

	core_not_off = psci_is_last_on_core_safe();
	if (core_not_off < PLATFORM_CORE_COUNT) {
		ERROR("DRTM: invalid launch due to non-boot PE not being turned off\n");
		return DENIED;
	}

	return SUCCESS;
}

static enum drtm_retc drtm_dl_prepare_dlme_data(const struct_drtm_dl_args *args,
                                                const drtm_event_log_t *ev_log,
                                                size_t *dlme_data_size_out);

/*
 * Note: accesses to the dynamic launch args, and to the DLME data are
 * little-endian as required, thanks to TF-A BL31 init requirements.
 */
static enum drtm_retc drtm_dl_check_args(uint64_t x1,
                                         struct_drtm_dl_args *a_out)
{
	uint64_t dlme_start, dlme_end;
	uint64_t dlme_img_start, dlme_img_ep, dlme_img_end;
	uint64_t dlme_data_start, dlme_data_end;
	uintptr_t args_mapping;
	size_t args_mapping_size;
	struct_drtm_dl_args *a;
	struct_drtm_dl_args args_buf;
	size_t dlme_data_size_req;
	struct __protected_regions protected_regions;
	int rc;
	enum drtm_retc ret;

	if (x1 % DRTM_PAGE_SIZE != 0) {
		ERROR("DRTM: parameters structure is not "
		      DRTM_PAGE_SIZE_STR "-aligned\n");
		return INVALID_PARAMETERS;
	}

	args_mapping_size = ALIGNED_UP(sizeof(struct_drtm_dl_args), DRTM_PAGE_SIZE);
	rc = mmap_add_dynamic_region_alloc_va(x1, &args_mapping, args_mapping_size,
	                           MT_MEMORY | MT_NS | MT_RO | MT_SHAREABILITY_ISH);
	if (rc) {
		WARN("DRTM: %s: mmap_add_dynamic_region() failed rc=%d\n",
		     __func__, rc);
		return INTERNAL_ERROR;
	}
	a = (struct_drtm_dl_args *)args_mapping;
	/*
	 * TODO: invalidate all data cache before reading the data passed by the
	 * DCE Preamble.  This is required to avoid / defend against racing with
	 * cache evictions.
	 */
	args_buf = *a;

	rc = mmap_remove_dynamic_region(args_mapping, args_mapping_size);
	if (rc) {
		ERROR("%s(): mmap_remove_dynamic_region() failed unexpectedly"
		      " rc=%d\n", __func__, rc);
		panic();
	}
	a = &args_buf;

	if (a->version != 1) {
		ERROR("DRTM: parameters structure incompatible with major version %d\n",
		      ARM_DRTM_VERSION_MAJOR);
		return NOT_SUPPORTED;
	}

	if (!(a->dlme_img_off   < a->dlme_size &&
	      a->dlme_data_off  < a->dlme_size)) {
		ERROR("DRTM: argument offset is outside of the DLME region\n");
		return INVALID_PARAMETERS;
	}
	dlme_start = a->dlme_paddr;
	dlme_end = a->dlme_paddr + a->dlme_size;
	dlme_img_start = a->dlme_paddr + a->dlme_img_off;
	dlme_img_ep = DL_ARGS_GET_DLME_ENTRY_POINT(a);
	dlme_img_end = dlme_img_start + a->dlme_img_size;
	dlme_data_start = a->dlme_paddr + a->dlme_data_off;
	dlme_data_end = dlme_end;

	/*
	 * TODO: validate that the DLME physical address range is all NS memory,
	 * return INVALID_PARAMETERS if it is not.
	 * Note that this check relies on platform-specific information. For
	 * examples, see psci_plat_pm_ops->validate_ns_entrypoint() or
	 * arm_validate_ns_entrypoint().
	 */

	/* Check the DLME regions arguments. */
	if (dlme_start % DRTM_PAGE_SIZE) {
		ERROR("DRTM: argument DLME region is not "
		      DRTM_PAGE_SIZE_STR "-aligned\n");
		return INVALID_PARAMETERS;
	}

	if (!(dlme_start < dlme_end &&
	      dlme_start <= dlme_img_start && dlme_img_start < dlme_img_end &&
	      dlme_start <= dlme_data_start && dlme_data_start < dlme_data_end)) {
		ERROR("DRTM: argument DLME region is discontiguous\n");
		return INVALID_PARAMETERS;
	}

	if (dlme_img_start < dlme_data_end && dlme_data_start < dlme_img_end) {
		ERROR("DRTM: argument DLME regions overlap\n");
		return INVALID_PARAMETERS;
	}

	/* Check the DLME image region arguments. */
	if (dlme_img_start % DRTM_PAGE_SIZE) {
		ERROR("DRTM: argument DLME image region is not "
		      DRTM_PAGE_SIZE_STR "-aligned\n");
		return INVALID_PARAMETERS;
	}

	if (!(dlme_img_start <= dlme_img_ep && dlme_img_ep < dlme_img_end)) {
		ERROR("DRTM: DLME entry point is outside of the DLME image region\n");
		return INVALID_PARAMETERS;
	}

	if (dlme_img_ep % 4) {
		ERROR("DRTM: DLME image entry point is not 4-byte-aligned\n");
		return INVALID_PARAMETERS;
	}

	/* Check the DLME data region arguments. */
	if (dlme_data_start % DRTM_PAGE_SIZE) {
		ERROR("DRTM: argument DLME data region is not "
		      DRTM_PAGE_SIZE_STR "-aligned\n");
		return INVALID_PARAMETERS;
	}

	rc = drtm_dl_prepare_dlme_data(NULL, NULL, &dlme_data_size_req);
	if (rc) {
		ERROR("%s: drtm_dl_prepare_dlme_data() failed unexpectedly rc=%d\n",
		      __func__, rc);
		panic();
	}
	if (dlme_data_end - dlme_data_start < dlme_data_size_req) {
		ERROR("DRTM: argument DLME data region is short of %lu bytes\n",
		      dlme_data_size_req - (size_t)(dlme_data_end - dlme_data_start));
		return INVALID_PARAMETERS;
	}

	/* Check the Normal World DCE region arguments. */
	if (a->dce_nwd_paddr != 0) {
		uint32_t dce_nwd_start = a->dce_nwd_paddr;
		uint32_t dce_nwd_end = dce_nwd_start + a->dce_nwd_size;

		if (!(dce_nwd_start < dce_nwd_end)) {
			ERROR("DRTM: argument Normal World DCE region is dicontiguous\n");
			return INVALID_PARAMETERS;
		}

		if (dce_nwd_start < dlme_end && dlme_start < dce_nwd_end) {
			ERROR("DRTM: argument Normal World DCE regions overlap\n");
			return INVALID_PARAMETERS;
		}
	}

	protected_regions = (struct __protected_regions) {
		.dlme_region = { a->dlme_paddr, a->dlme_size },
		.dce_nwd_region = { a->dce_nwd_paddr, a->dce_nwd_size },
	};
	if ((ret = drtm_dma_prot_check_args(&a->dma_prot_args,
	                                    DL_ARGS_GET_DMA_PROT_TYPE(a),
	                                    protected_regions))){
		return ret;
	}

	*a_out = *a;
	return SUCCESS;
}

static enum drtm_retc drtm_dl_prepare_dlme_data(const struct_drtm_dl_args *args,
                                        const drtm_event_log_t *drtm_event_log,
                                        size_t *dlme_data_size_out)
{
	int rc;
	size_t dlme_data_total_bytes_req = 0;
	uint64_t dlme_data_paddr;
	size_t dlme_data_max_size;
	uintptr_t dlme_data_mapping;
	size_t dlme_data_mapping_bytes;
	struct_dlme_data_header *dlme_data_hdr;
	char *dlme_data_cursor;
	size_t dlme_prot_tables_bytes;
	const char *dlme_addr_map;
	size_t dlme_addr_map_bytes;
	size_t drtm_event_log_bytes;
	size_t drtm_tcb_hashes_bytes;
	size_t serialised_bytes_actual;

	/* Size the DLME protected regions. */
	drtm_dma_prot_serialise_table(NULL, &dlme_prot_tables_bytes);
	dlme_data_total_bytes_req += dlme_prot_tables_bytes;

	/* Size the DLME address map. */
	drtm_cache_get_resource("address-map",
	                        &dlme_addr_map, &dlme_addr_map_bytes);
	dlme_data_total_bytes_req += dlme_addr_map_bytes;

	/* Size the DRTM event log. */
	drtm_serialise_event_log(NULL, drtm_event_log, &drtm_event_log_bytes);
	dlme_data_total_bytes_req += drtm_event_log_bytes;

	/* Size the TCB hashes table. */
	drtm_serialise_tcb_hashes_table(NULL, &drtm_tcb_hashes_bytes);
	dlme_data_total_bytes_req += drtm_tcb_hashes_bytes;

	/* Size the implementation-specific DLME region. */

	if (args == NULL) {
		if (dlme_data_size_out) {
			*dlme_data_size_out = dlme_data_total_bytes_req;
		}
		return SUCCESS;
	}

	dlme_data_paddr = args->dlme_paddr + args->dlme_data_off;
	dlme_data_max_size = args->dlme_size - args->dlme_data_off;

	/*
	 * The capacity of the given DLME data region is checked when
	 * the other dynamic launch arguments are.
	 */
	if (dlme_data_max_size < dlme_data_total_bytes_req) {
		ERROR("%s: assertion failed:"
		      " dlme_data_max_size (%ld) < dlme_data_total_bytes_req (%ld)\n",
		      __func__, dlme_data_max_size, dlme_data_total_bytes_req);
		panic();
	}

	/* Map the DLME data region as NS memory. */
	dlme_data_mapping_bytes = ALIGNED_UP(dlme_data_max_size, DRTM_PAGE_SIZE);
	rc = mmap_add_dynamic_region_alloc_va(dlme_data_paddr, &dlme_data_mapping,
	         dlme_data_mapping_bytes, MT_RW_DATA | MT_NS | MT_SHAREABILITY_ISH);
	if (rc) {
		WARN("DRTM: %s: mmap_add_dynamic_region() failed rc=%d\n", __func__, rc);
		return INTERNAL_ERROR;
	}
	dlme_data_hdr = (struct_dlme_data_header *)dlme_data_mapping;
	dlme_data_cursor = (char *)dlme_data_hdr + sizeof(*dlme_data_hdr);

	/* Set the header version and size. */
	dlme_data_hdr->version = 1;
	dlme_data_hdr->this_hdr_size = sizeof(*dlme_data_hdr);

	/* Prepare DLME protected regions. */
	drtm_dma_prot_serialise_table(dlme_data_cursor, &serialised_bytes_actual);
	assert(serialised_bytes_actual == dlme_prot_tables_bytes);
	dlme_data_hdr->dlme_prot_regions_size = dlme_prot_tables_bytes;
	dlme_data_cursor += dlme_prot_tables_bytes;

	/* Prepare DLME address map. */
	if (dlme_addr_map) {
		memcpy(dlme_data_cursor, dlme_addr_map, dlme_addr_map_bytes);
	} else {
		WARN("DRTM: DLME address map is not in the cache\n");
	}
	dlme_data_hdr->dlme_addr_map_size = dlme_addr_map_bytes;
	dlme_data_cursor += dlme_addr_map_bytes;

	/* Prepare DRTM event log for DLME. */
	drtm_serialise_event_log(dlme_data_cursor, drtm_event_log,
	                         &serialised_bytes_actual);
	assert(serialised_bytes_actual <= drtm_event_log_bytes);
	dlme_data_hdr->dlme_tpm_log_size = serialised_bytes_actual;
	dlme_data_cursor += serialised_bytes_actual;

	/* Prepare the TCB hashes for DLME. */
	drtm_serialise_tcb_hashes_table(dlme_data_cursor, &serialised_bytes_actual);
	assert(serialised_bytes_actual == drtm_tcb_hashes_bytes);
	dlme_data_hdr->dlme_tcb_hashes_table_size = drtm_tcb_hashes_bytes;
	dlme_data_cursor += drtm_tcb_hashes_bytes;

	/* Implementation-specific region size is unused. */
	dlme_data_hdr->dlme_impdef_region_size = 0;
	dlme_data_cursor += 0;

	/* Prepare DLME data size. */
	dlme_data_hdr->dlme_data_size = dlme_data_cursor - (char *)dlme_data_hdr;

	/* Unmap the DLME data region. */
	rc = mmap_remove_dynamic_region(dlme_data_mapping, dlme_data_mapping_bytes);
	if (rc) {
		ERROR("%s(): mmap_remove_dynamic_region() failed"
		      " unexpectedly rc=%d\n", __func__, rc);
		panic();
	}

	if (dlme_data_size_out) {
		*dlme_data_size_out = dlme_data_total_bytes_req;
	}
	return SUCCESS;
}

static void drtm_dl_reset_dlme_el_state(enum drtm_dlme_el dlme_el)
{
	uint64_t sctlr;

	/*
	 * TODO: Set PE state according to the PSCI's specification of the initial
	 * state after CPU_ON, or to reset values if unspecified, where they exist,
	 * or define sensible values otherwise.
	 */

	switch (dlme_el) {
	case DLME_AT_EL1:
		sctlr = read_sctlr_el1();
		break;

	case DLME_AT_EL2:
		sctlr = read_sctlr_el2();
		break;

	default: /* Not reached */
		ERROR("%s(): dlme_el has the unexpected value %d\n",
		      __func__, dlme_el);
		panic();
	}

	sctlr &= ~(
	/* Disable DLME's EL MMU, since the existing page-tables are untrusted. */
	        SCTLR_M_BIT
	      | SCTLR_EE_BIT               /* Little-endian data accesses. */
	);

	sctlr |=
	        SCTLR_C_BIT | SCTLR_I_BIT  /* Allow instruction and data caching. */
	;

	switch (dlme_el) {
	case DLME_AT_EL1:
		write_sctlr_el1(sctlr);
		break;

	case DLME_AT_EL2:
		write_sctlr_el2(sctlr);
		break;
	}
}

static void drtm_dl_reset_dlme_context(enum drtm_dlme_el dlme_el)
{
	void *ns_ctx = cm_get_context(NON_SECURE);
	gp_regs_t *gpregs = get_gpregs_ctx(ns_ctx);
	uint64_t spsr_el3 = read_ctx_reg(get_el3state_ctx(ns_ctx), CTX_SPSR_EL3);

	/* Reset all gpregs, including SP_EL0. */
	memset(gpregs, 0, sizeof(*gpregs));

	/* Reset SP_ELx. */
	switch (dlme_el) {
	case DLME_AT_EL1:
		write_sp_el1(0);
		break;

	case DLME_AT_EL2:
		write_sp_el2(0);
		break;
	}

	/*
	 * DLME's async exceptions are masked to avoid a NWd attacker's timed
	 * interference with any state we established trust in or measured.
	 */
	spsr_el3 |= SPSR_DAIF_MASK << SPSR_DAIF_SHIFT;

	write_ctx_reg(get_el3state_ctx(ns_ctx), CTX_SPSR_EL3, spsr_el3);
}

static void drtm_dl_prepare_eret_to_dlme(const struct_drtm_dl_args *args,
                                         enum drtm_dlme_el dlme_el)
{
	void *ctx = cm_get_context(NON_SECURE);
	uint64_t dlme_ep = DL_ARGS_GET_DLME_ENTRY_POINT(args);
	uint64_t spsr_el3 = read_ctx_reg(get_el3state_ctx(ctx), CTX_SPSR_EL3);

	/* Next ERET is to the DLME's EL. */
	spsr_el3 &= ~(MODE_EL_MASK << MODE_EL_SHIFT);
	switch (dlme_el) {
	case DLME_AT_EL1:
		spsr_el3 |= MODE_EL1 << MODE_EL_SHIFT;
		break;

	case DLME_AT_EL2:
		spsr_el3 |= MODE_EL2 << MODE_EL_SHIFT;
		break;
	}

	/* Next ERET is to the DLME entry point. */
	cm_set_elr_spsr_el3(NON_SECURE, dlme_ep, spsr_el3);
}

/*
 * TODO:
 * - Close locality 3;
 * - See section 4.4 and section 4.5 for other requirements;
 */
static uint64_t drtm_dynamic_launch(uint64_t x1, void *handle)
{
	enum drtm_retc ret;
	struct_drtm_dl_args args;
	enum drtm_dlme_el dlme_el;
	drtm_event_log_t event_log;

	/*
	 * Non-secure interrupts are masked to avoid a NWd attacker's timed
	 * interference with any state we are establishing trust in or measuring.
	 * Note that in this particular implementation, both Non-secure and Secure
	 * interrupts are automatically masked consequence of the SMC call.
	 */

	if ((ret = drtm_dl_check_caller_el(handle))) {
		SMC_RET1(handle, ret);
	}

	if ((ret = drtm_dl_check_cores())) {
		SMC_RET1(handle, ret);
	}

	if ((ret = drtm_dl_check_args(x1, &args))) {
		SMC_RET1(handle, ret);
	}

	drtm_dl_ensure_tcb_hashes_are_final();

	/*
	 * Engage the DMA protections.  The launch cannot proceed without the DMA
	 * protections due to potential TOC/TOU vulnerabilities w.r.t. the DLME
	 * region (and to the NWd DCE region).
	 */
	if ((ret = drtm_dma_prot_engage(&args.dma_prot_args,
	                                DL_ARGS_GET_DMA_PROT_TYPE(&args)))) {
		SMC_RET1(handle, ret);
	}

	/*
	 * The DMA protection is now engaged.  Note that any failure mode that
	 * returns an error to the DRTM-launch caller must now disengage DMA
	 * protections before returning to the caller.
	 */

	if ((ret = drtm_take_measurements(&args, &event_log))) {
		goto err_undo_dma_prot;
	}

	if ((ret = drtm_dl_prepare_dlme_data(&args, &event_log, NULL))) {
		goto err_undo_dma_prot;
	}

	/*
	 * Note that, at the time of writing, the DRTM spec allows a successful
	 * launch from NS-EL1 to return to a DLME in NS-EL2.  The practical risk
	 * of a privilege escalation, e.g. due to a compromised hypervisor, is
	 * considered small enough not to warrant the specification of additional
	 * DRTM conduits that would be necessary to maintain OSs' abstraction from
	 * the presence of EL2 were the dynamic launch only be allowed from the
	 * highest NS EL.
	 */
	dlme_el = drtm_dlme_el(get_highest_ns_el_implemented());

	drtm_dl_reset_dlme_el_state(dlme_el);
	drtm_dl_reset_dlme_context(dlme_el);

	/*
	 * TODO: Reset all SDEI event handlers, since they are untrusted.  Both
	 * private and shared events for all cores must be unregistered.
	 * Note that simply calling SDEI ABIs would not be adequate for this, since
	 * there is currently no SDEI operation that clears private data for all PEs.
	 */

	drtm_dl_prepare_eret_to_dlme(&args, dlme_el);

	/*
	 * TODO: invalidate the instruction cache before jumping to the DLME.
	 * This is required to defend against potentially-malicious cache contents.
	 */

	/* Return the DLME region's address in x0, and the DLME data offset in x1.*/
	SMC_RET2(handle, args.dlme_paddr, args.dlme_data_off);

err_undo_dma_prot:
	;
	int rc;

	if ((rc = drtm_dma_prot_disengage())) {
		ERROR("%s(): drtm_dma_prot_disengage() failed unexpectedly"
		      " rc=%d\n", __func__, rc);
		panic();
	}
	SMC_RET1(handle, ret);
}


static uint64_t drtm_features_tpm(void *ctx)
{
	SMC_RET2(ctx, 1ULL,       /* TPM feature is supported */
	         1ULL << 33       /* Default PCR usage schema */
	         | 0ULL << 32     /* Firmware-based hashing */
	         /* The firmware hashing algorithm */
	         | (uint32_t)DRTM_TPM_HASH_ALG << 0
	);
}

static uint64_t drtm_features_mem_req(void *ctx)
{
	int rc;
	size_t dlme_data_bytes_req;
	uint64_t dlme_data_pages_req;

	rc = drtm_dl_prepare_dlme_data(NULL, NULL, &dlme_data_bytes_req);
	if (rc) {
		ERROR("%s(): drtm_dl_prepare_dlme_data() failed unexpectedly"
		      " rc=%d\n", __func__, rc);
		panic();
	}

	dlme_data_pages_req = ALIGNED_UP(dlme_data_bytes_req, DRTM_PAGE_SIZE)
	                      / DRTM_PAGE_SIZE;
	if (dlme_data_pages_req > UINT32_MAX) {
		ERROR("%s(): dlme_data_pages_req is unexpectedly large"
		      " (does not fit in the bit-field)\n", __func__);
		panic();
	}

	SMC_RET2(ctx, 1ULL,     /* Feature is supported */
	         0ULL << 32     /* Not using a Normal World DCE */
	         /* Minimum amount of space needed for the DLME data */
	         | (dlme_data_pages_req & 0xffffffffULL)
	);
}

static uint64_t drtm_features_boot_pe_id(void *ctx)
{
	SMC_RET2(ctx, 1ULL,         /* Boot PE feature is supported */
	         boot_pe_aff_value  /* Boot PE identification */
	);
}


uint64_t drtm_smc_handler(uint32_t smc_fid,
		uint64_t x1,
		uint64_t x2,
		uint64_t x3,
		uint64_t x4,
		void *cookie,
		void *handle,
		uint64_t flags)
{
	/* Check that the SMC call is from the Normal World. */
	if (is_caller_secure(flags)) {
		SMC_RET1(handle, NOT_SUPPORTED);
	}

	switch (smc_fid) {
	case ARM_DRTM_SVC_VERSION:
		INFO("++ DRTM service handler: version\n");
		/* Return the version of current implementation */
		SMC_RET1(handle, ARM_DRTM_VERSION);

	case ARM_DRTM_SVC_FEATURES:
		if ((x1 >> 63 & 0x1U) == 0) {
			uint32_t func_id = x1;

			/* Dispatch function-based queries. */
			switch (func_id) {
			case ARM_DRTM_SVC_VERSION:
				INFO("++ DRTM service handler: DRTM_VERSION feature\n");
				SMC_RET1(handle, SUCCESS);

			case ARM_DRTM_SVC_FEATURES:
				INFO("++ DRTM service handler: DRTM_FEATURES feature\n");
				SMC_RET1(handle, SUCCESS);

			case ARM_DRTM_SVC_UNPROTECT_MEM:
				INFO("++ DRTM service handler: DRTM_UNPROTECT_MEMORY feature\n");
				SMC_RET1(handle, SUCCESS);

			case ARM_DRTM_SVC_DYNAMIC_LAUNCH:
				INFO("++ DRTM service handler: DRTM_DYNAMIC_LAUNCH feature\n");
				SMC_RET1(handle, SUCCESS);

			case ARM_DRTM_SVC_CLOSE_LOCALITY:
				INFO("++ DRTM service handler: DRTM_CLOSE_LOCALITY feature\n");
				SMC_RET1(handle, NOT_SUPPORTED);

			case ARM_DRTM_SVC_GET_ERROR:
				INFO("++ DRTM service handler: DRTM_GET_ERROR feature\n");
				SMC_RET1(handle, NOT_SUPPORTED);

			case ARM_DRTM_SVC_SET_ERROR:
				INFO("++ DRTM service handler: DRTM_SET_ERROR feature\n");
				SMC_RET1(handle, NOT_SUPPORTED);

			case ARM_DRTM_SVC_SET_TCB_HASH:
				INFO("++ DRTM service handler: DRTM_SET_TCB_HASH feature\n");
				SMC_RET1(handle, NOT_SUPPORTED);

			case ARM_DRTM_SVC_LOCK_TCB_HASHES:
				INFO("++ DRTM service handler: DRTM_LOCK_TCB_HASHES feature\n");
				SMC_RET1(handle, NOT_SUPPORTED);

			default:
				ERROR("Unknown ARM DRTM service function feature\n");
				SMC_RET1(handle, NOT_SUPPORTED);
			}
		} else {
			uint8_t feat_id = x1;

			/* Dispatch feature-based queries. */
			switch (feat_id) {
			case ARM_DRTM_FEATURES_TPM:
				INFO("++ DRTM service handler: TPM features\n");
				return drtm_features_tpm(handle);

			case ARM_DRTM_FEATURES_MEM_REQ:
				INFO("++ DRTM service handler: Min. mem."
				     " requirement features\n");
				return drtm_features_mem_req(handle);

			case ARM_DRTM_FEATURES_DMA_PROT:
				INFO("++ DRTM service handler: DMA protection features\n");
				return drtm_features_dma_prot(handle);

			case ARM_DRTM_FEATURES_BOOT_PE_ID:
				INFO("++ DRTM service handler: Boot PE ID features\n");
				return drtm_features_boot_pe_id(handle);

			case ARM_DRTM_FEATURES_TCB_HASHES:
				INFO("++ DRTM service handler: TCB-hashes features\n");
				return drtm_features_tcb_hashes(handle);

			default:
				ERROR("Unknown ARM DRTM service feature\n");
				SMC_RET1(handle, NOT_SUPPORTED);
			}
		}

	case ARM_DRTM_SVC_UNPROTECT_MEM:
		INFO("++ DRTM service handler: unprotect mem\n");
		return drtm_unprotect_mem(handle);

	case ARM_DRTM_SVC_DYNAMIC_LAUNCH:
		INFO("++ DRTM service handler: dynamic launch\n");
		//locality2 = 1;
		//locality3 = 1;
		return drtm_dynamic_launch(x1, handle);

	case ARM_DRTM_SVC_CLOSE_LOCALITY:
		INFO("++ DRTM service handler: close locality\n");
		if (x1 == 2) {
			if (locality2 == 1) {
				locality2 = 0;
				SMC_RET1(handle, SMC_OK);
			}
			SMC_RET1(handle, DENIED);
		}
		if (x1 == 3) {
			if (locality3 == 1) {
				locality3 = 0;
				SMC_RET1(handle, SMC_OK);
			}
			SMC_RET1(handle, DENIED);
		}
		SMC_RET1(handle, INVALID_PARAMETERS);

	case ARM_DRTM_SVC_GET_ERROR:
		INFO("++ DRTM service handler: get error\n");
		return drtm_get_error(handle);

	case ARM_DRTM_SVC_SET_ERROR:
		INFO("++ DRTM service handler: set error\n");
		return drtm_set_error(x1, handle);

	default:
		ERROR("Unknown ARM DRTM service call: 0x%x \n", smc_fid);
		SMC_RET1(handle, SMC_UNK);
	}
}
