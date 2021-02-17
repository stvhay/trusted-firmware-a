/*
 * Copyright (c) 2021, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch_helpers.h>
#include <assert.h>
#include <errno.h>
#include <libfdt.h>

#include <bl31/bl31.h>
#include <bl31/ehf.h>
#include <common/debug.h>
#include <common/fdt_wrappers.h>
#include <common/runtime_svc.h>
#include <dt-bindings/memory/memory.h>
#include <lib/el3_runtime/context_mgmt.h>
#include <lib/smccc.h>
#include <lib/utils.h>
#include <plat/common/platform.h>
#include <services/ffa_svc.h>
#include <services/spmc_svc.h>

#include "spmc.h"
#include "spm_shim_private.h"

static spmc_sp_context_t spmc_sp_ctx[SECURE_PARTITION_COUNT];
static unsigned int next_available_sp_index;
static unsigned int schedule_sp_index;

static void *spmc_manifest;

static int get_sp_index(uint16_t sp_id, unsigned int *sp_index)
{
	int i;

	for (i = 0; i < SECURE_PARTITION_COUNT; i++) {
		if (sp_id == spmc_sp_ctx[i].sp_id) {
			*sp_index = i;
			return i;
		}
	}

	return -EINVAL;
}

/*******************************************************************************
 * This function will parse the Secure Partition Manifest for fetching seccure
 * partition specific memory region details. It will find base address, size,
 * memory attributes for each memory region and then add the respective region
 * into secure parition's translation context.
 ******************************************************************************/
static void populate_sp_mem_regions(sp_context_t *sp_ctx,
				    void *sp_manifest,
				    int node)
{
	uintptr_t base_address, size;
	uint32_t mem_attr, granularity, mem_region;
	struct mmap_region sp_mem_regions;
	int32_t offset, ret;

	for (offset = fdt_first_subnode(sp_manifest, node), mem_region = 0;
	     offset >= 0;
	     offset = fdt_next_subnode(sp_manifest, offset), mem_region++) {
		if (offset < 0)
			WARN("Error happened in SPMC manifest bootargs reading\n");
		else {
			ret = fdt_get_reg_props_by_index(sp_manifest, offset,
							 0, &base_address,
							 &size);
			if (ret < 0) {
				WARN("Missing reg property for Mem region %u.\n", mem_region);
				continue;
			}

			ret = fdt_read_uint32(sp_manifest,
					      offset, "mem_region_access",
					      &mem_attr);
			if (ret < 0) {
				WARN("Missing Mem region %u access attributes.\n", mem_region);
				continue;
			}

			sp_mem_regions.attr = MT_USER;
			if (mem_attr == MEM_CODE)
				sp_mem_regions.attr |= MT_CODE;
			else if (mem_attr == MEM_RO_DATA)
				sp_mem_regions.attr |= MT_RO_DATA;
			else if (mem_attr == MEM_RW_DATA)
				sp_mem_regions.attr |= MT_RW_DATA;
			else if (mem_attr == MEM_RO)
				sp_mem_regions.attr |= MT_RO;
			else if (mem_attr == MEM_RW)
				sp_mem_regions.attr |= MT_RW;

			ret = fdt_read_uint32(sp_manifest,
					      offset, "mem_region_type",
					      &mem_attr);
			if (ret < 0) {
				WARN("Missing Mem region %u type.\n", mem_region);
				continue;
			}

			if (mem_attr == MEM_DEVICE)
				sp_mem_regions.attr |= MT_DEVICE;
			else if (mem_attr == MEM_NON_CACHE)
				sp_mem_regions.attr |= MT_NON_CACHEABLE;
			else if (mem_attr == MEM_NORMAL)
				sp_mem_regions.attr |= MT_MEMORY;

			ret = fdt_read_uint32(sp_manifest,
					      offset,
					      "mem_region_secure",
					      &mem_attr);
			if (ret < 0) {
				WARN("Missing Mem region %u secure state.\n", mem_region);
				continue;
			}

			if (mem_attr == MEM_SECURE)
				sp_mem_regions.attr |= MT_SECURE;
			else if (mem_attr == MEM_NON_SECURE)
				sp_mem_regions.attr |= MT_NS;

			ret = fdt_read_uint32(sp_manifest,
					      offset, "granularity",
					      &granularity);
			if (ret < 0) {
				WARN("Missing Mem region %u granularity.\n", mem_region);
				continue;
			}
			sp_mem_regions.base_pa = base_address;
			sp_mem_regions.base_va = base_address;
			sp_mem_regions.size = size;
			sp_mem_regions.granularity = granularity;
			mmap_add_region_ctx(sp_ctx->xlat_ctx_handle,
					    &sp_mem_regions);
		}
	}
}

/*******************************************************************************
 * This function will parse the Secure Partition Manifest. From manifest, it
 * will fetch details for preparing Secure partition image context and secure
 * partition image  boot arguments if any. Also if there are memory regions
 * present in secure partition manifest then it will invoke function to map
 * respective memory regions.
 ******************************************************************************/
static int sp_manifest_parse(void *sp_manifest, int offset,
			     sp_context_t *sp_ctx,
			     entry_point_info_t *ep_info)
{
	int32_t ret, node;

	node = fdt_subnode_offset_namelen(sp_manifest, offset,
					  "ffa-config",
					  sizeof("ffa-config") - 1);
	if (node < 0)
		WARN("Not found any ffa-config for SP.\n");
	else {
		uint64_t config;
		uint32_t config_32;

		ret = fdt_read_uint32(sp_manifest, node,
				      "partition_id", &config_32);
		if (ret)
			WARN("Missing Secure Partition ID.\n");
		else
			spmc_sp_ctx[next_available_sp_index].sp_id = config_32;

		ret = fdt_read_uint64(sp_manifest, node,
				      "sp_arg0", &config);
		if (ret)
			WARN("Missing Secure Partition arg0.\n");
		else
			ep_info->args.arg0 = config;

		ret = fdt_read_uint64(sp_manifest, node,
				      "sp_arg1", &config);
		if (ret)
			WARN("Missing Secure Partition  arg1.\n");
		else
			ep_info->args.arg1 = config;

		ret = fdt_read_uint64(sp_manifest, node,
				      "sp_arg2", &config);
		if (ret)
			WARN("Missing Secure Partition  arg2.\n");
		else
			ep_info->args.arg2 = config;

		ret = fdt_read_uint64(sp_manifest, node,
				      "sp_arg3", &config);
		if (ret)
			WARN("Missing Secure Partition  arg3.\n");
		else
			ep_info->args.arg3 = config;

		ret = fdt_read_uint64(sp_manifest, node,
				      "load_address", &config);
		if (ret)
			WARN("Missing Secure Partition Entry Point.\n");
		else
			ep_info->pc = config;

		ret = fdt_read_uint64(sp_manifest, node,
				      "stack_base", &config);
		if (ret)
			WARN("Missing Secure Partition Stack Base.\n");
		else
			sp_ctx->sp_stack_base = config;

		ret = fdt_read_uint64(sp_manifest, node,
				      "stack_size", &config);
		if (ret)
			WARN("Missing Secure Partition Stack Size.\n");
		else
			sp_ctx->sp_pcpu_stack_size = config;

		ret = fdt_read_uint32(sp_manifest, node,
				      "runtime-el", &config_32);
		if (ret)
			WARN("Missing SP Runtime EL information.\n");
		else {
			if (config_32 == 0) {
				/* Setup Secure Partition SPSR */
				ep_info->spsr =
					SPSR_64(MODE_EL0, MODE_SP_EL0,
						DISABLE_ALL_EXCEPTIONS);

				sp_ctx->xlat_ctx_handle->xlat_regime =
								EL1_EL0_REGIME;

				/* This region contains the exception
				 * vectors used at S-EL1.
				 */
				mmap_region_t sel1_exception_vectors =
					MAP_REGION_FLAT(SPM_SHIM_EXCEPTIONS_START,
							SPM_SHIM_EXCEPTIONS_SIZE,
							MT_CODE | MT_SECURE | MT_PRIVILEGED);
				mmap_add_region_ctx(sp_ctx->xlat_ctx_handle,
						    &sel1_exception_vectors);
			}
		}
	}

	node = fdt_subnode_offset_namelen(sp_manifest, offset,
					  "mem-regions",
					  sizeof("mem-regions") - 1);
	if (node < 0)
		WARN("Not found mem-region configuration for SP.\n");
	else {
		populate_sp_mem_regions(sp_ctx, sp_manifest, node);
	}

	return 0;
}

/*******************************************************************************
 * This function gets the Secure Partition Manifest base and maps the manifest
 * region.
 * Currently, one Secure partition manifest is considered and prepared the
 * Secure Partition context for the same.
 *
 ******************************************************************************/
static int find_and_prepare_sp_context(void)
{
	void *sp_manifest;
	uintptr_t manifest_base, manifest_base_align;
	entry_point_info_t *next_image_ep_info;
	int32_t ret;

	next_image_ep_info = bl31_plat_get_next_image_ep_info(SECURE);
	if (next_image_ep_info == NULL) {
		WARN("TEST: No Secure Partition image provided by BL2\n");
		return -ENOENT;
	}

	sp_manifest = (void *)next_image_ep_info->args.arg0;
	if (sp_manifest == NULL) {
		WARN("Secure Partition(SP) manifest absent\n");
		return -ENOENT;
	}

	manifest_base = (uintptr_t)sp_manifest;
	manifest_base_align = page_align(manifest_base, UP);

	manifest_base_align = page_align(manifest_base, DOWN);

	/* Map the secure partition manifest region in the EL3 translation regime.
	 * Map an area equal to (2 * PAGE_SIZE) for now. During manifest base
	 * alignment the region of 1 PAGE_SIZE from manifest align base may not
	 * completely accommodate the secure partition manifest region.
	 */
	ret = mmap_add_dynamic_region((unsigned long long)manifest_base_align,
				      manifest_base_align,
				      PAGE_SIZE * 2,
				      MT_RO_DATA);
	if (ret != 0) {
		ERROR("Error while mapping SP manifest (%d).\n", ret);
		return ret;
	}

	ret = fdt_node_offset_by_compatible(sp_manifest, -1, "arm,ffa-manifest");
	if (ret < 0) {
		ERROR("Error happened in SP manifest reading.\n");
		return -EINVAL;
	} else {
		sp_context_t *sp_ctx = &(spmc_sp_ctx[next_available_sp_index].sp_ctx);
		cpu_context_t *cpu_ctx = &(sp_ctx->cpu_ctx);

		/* Assign translation tables context. */
		sp_ctx->xlat_ctx_handle = spm_get_sp_xlat_context();

		/*
		 * Initialize CPU context
		 * ----------------------
		 */
		entry_point_info_t ep_info = {0};

		SET_PARAM_HEAD(&ep_info, PARAM_EP, VERSION_1, SECURE | EP_ST_ENABLE);

		ret = sp_manifest_parse(sp_manifest, ret, sp_ctx, &ep_info);
		if (ret) {
			ERROR(" Error in Secure Partition(SP) manifest parsing.\n");
			return ret;
		}

		cm_setup_context(cpu_ctx, &ep_info);
		write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_SP_EL0,
				sp_ctx->sp_stack_base + sp_ctx->sp_pcpu_stack_size);

		schedule_sp_index = next_available_sp_index;
		spm_sp_setup(sp_ctx);
		next_available_sp_index++;
	}

	return 0;
}

static int32_t sp_init(void)
{
	uint64_t rc;
	sp_context_t *ctx;

	INFO("Secure Partition init start.\n");

	ctx = &(spmc_sp_ctx[schedule_sp_index].sp_ctx);
	ctx->state = SP_STATE_RESET;

	rc = spm_sp_synchronous_entry(ctx);
	assert(rc == 0);

	ctx->state = SP_STATE_IDLE;

	INFO("Secure Partition initialized.\n");

	return !rc;
}

/*******************************************************************************
 * Function to perform a call to a Secure Partition.
 ******************************************************************************/
static uint64_t spmc_sp_call(unsigned int sp_index, uint32_t smc_fid,
			     uint64_t comm_buffer_address,
			     uint64_t comm_size, uint64_t core_pos)
{
	uint64_t rc;
	sp_context_t *sp_ptr = &(spmc_sp_ctx[sp_index].sp_ctx);

	/* Set values for registers on SP entry */
	cpu_context_t *cpu_ctx = &(sp_ptr->cpu_ctx);

	write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_X0, smc_fid);
	write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_X1, 0);
	write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_X2, 0);
	write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_X3, comm_buffer_address);
	write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_X4, comm_size);
	write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_X5, 0);
	write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_X6, core_pos);

	/* Jump to the Secure Partition. */
	rc = spm_sp_synchronous_entry(sp_ptr);

	return rc;
}

/*******************************************************************************
 * Return FFA_ERROR with specified error code
 ******************************************************************************/
static uint64_t spmc_ffa_error_return(void *handle, int error_code)
{
	SMC_RET8(handle, FFA_ERROR,
		 FFA_TARGET_INFO_MBZ, error_code,
		 FFA_PARAM_MBZ, FFA_PARAM_MBZ, FFA_PARAM_MBZ,
		 FFA_PARAM_MBZ, FFA_PARAM_MBZ);
}

/*******************************************************************************
 * MM_INTERFACE handler
 ******************************************************************************/
static uint64_t spmc_mm_interface_handler(unsigned int sp_index,
					  uint32_t smc_fid,
					  uint64_t mm_cookie,
					  uint64_t comm_buffer_address,
					  uint64_t comm_size_address,
					  void *handle)
{
	uint64_t rc;

	/*
	 * The current secure partition design mandates
	 * - at any point, only a single core can be
	 *   executing in the secure partition.
	 * - a core cannot be preempted by an interrupt
	 *   while executing in secure partition.
	 * Raise the running priority of the core to the
	 * interrupt level configured for secure partition
	 * so as to block any interrupt from preempting this
	 * core.
	 */
	ehf_activate_priority(PLAT_SP_PRI);

	/* Save the Normal world context */
	cm_el1_sysregs_context_save(NON_SECURE);

	rc = spmc_sp_call(sp_index, smc_fid, comm_buffer_address,
			  comm_size_address, plat_my_core_pos());

	/* Restore non-secure state */
	cm_el1_sysregs_context_restore(NON_SECURE);
	cm_set_next_eret_context(NON_SECURE);

	/*
	 * Exited from secure partition. This core can take
	 * interrupts now.
	 */
	ehf_deactivate_priority(PLAT_SP_PRI);

	if (rc == 0)
		SMC_RET2(handle, FFA_SUCCESS_SMC64, 0x7);

	return spmc_ffa_error_return(handle,
				     FFA_ERROR_NOT_SUPPORTED);
}

static uint64_t direct_req_secure_smc_handler(uint64_t x1, uint64_t x2,
					      uint64_t x3, uint64_t x4,
					      void *cookie, void *handle,
					      uint64_t flags)
{
	uint64_t rc;
	uint16_t sp_id;
	unsigned int sp_index;
	sp_context_t *sp_ctx;

	/* Make next ERET jump to S-EL0 instead of S-EL1. */
	cm_set_elr_spsr_el3(SECURE, read_elr_el1(), read_spsr_el1());

	switch (x3) {
	case SP_MEMORY_ATTRIBUTES_GET_AARCH64:
		INFO("Received SP_MEMORY_ATTRIBUTES_GET_AARCH64 request\n");

		sp_id = STMM_SP_ID;
		if (get_sp_index(sp_id, &sp_index) < 0) {
			WARN("Not found the StMM Secure Partition.\n");
			break;
		}

		sp_ctx = &(spmc_sp_ctx[sp_index].sp_ctx);

		if (sp_ctx->state != SP_STATE_RESET) {
			WARN("SP_MEMORY_ATTRIBUTES_GET_AARCH64 is available at boot time\n");
			return spmc_ffa_error_return(handle,
						     FFA_ERROR_NOT_SUPPORTED);
		}

		rc = spm_memory_attributes_get_smc_handler(sp_ctx, x4);
		if (rc < 0)
			return spmc_ffa_error_return(handle,
						     FFA_ERROR_INVALID_PARAMETER);

		SMC_RET4(handle, FFA_MSG_SEND_DIRECT_RESP_SMC64, 0x0, 0x0, rc);

	case SP_MEMORY_ATTRIBUTES_SET_AARCH64:
		INFO("Received SP_MEMORY_ATTRIBUTES_SET_AARCH64 request\n");

		sp_id = STMM_SP_ID;
		if (get_sp_index(sp_id, &sp_index) < 0) {
			WARN("Not found the StMM Secure Partition.\n");
			break;
		}
		sp_ctx = &(spmc_sp_ctx[sp_index].sp_ctx);

		if (sp_ctx->state != SP_STATE_RESET) {
			WARN("SP_MEMORY_ATTRIBUTES_SET_AARCH64 is available at boot time\n");
			return spmc_ffa_error_return(handle,
						     FFA_ERROR_NOT_SUPPORTED);
		}

		rc =  spm_memory_attributes_set_smc_handler(sp_ctx, x4,
					SMC_GET_GP(handle, CTX_GPREG_X5),
					SMC_GET_GP(handle, CTX_GPREG_X6));

		if (rc < 0)
			return spmc_ffa_error_return(handle,
						     FFA_ERROR_INVALID_PARAMETER);

		SMC_RET4(handle, FFA_MSG_SEND_DIRECT_RESP_SMC64, 0x0, 0x0, rc);

	default:
		WARN("Not supported direct request handling for ID=0x%llx\n", x3);
		break;
	}

	return spmc_ffa_error_return(handle,
			FFA_ERROR_NOT_SUPPORTED);
}

static uint64_t direct_req_non_secure_smc_handler(uint64_t x1,
						  uint64_t x2,
						  uint64_t x3,
						  uint64_t x4,
						  void *cookie,
						  void *handle,
						  uint64_t flags)
{
	uint16_t dst_id;
	unsigned int sp_index;

	switch (x3) {
	case MM_INTERFACE_ID_AARCH32:
	case MM_INTERFACE_ID_AARCH64:
		INFO("MM interface id\n");
		dst_id = STMM_SP_ID;
		if (get_sp_index(dst_id, &sp_index) < 0) {
			WARN("Not found the StMM Secure Partition.\n");
			break;
		}
		return spmc_mm_interface_handler(sp_index, x3, x4,
				SMC_GET_GP(handle, CTX_GPREG_X5),
				SMC_GET_GP(handle, CTX_GPREG_X6), handle);
	default:
		WARN("Not supported direct request handling for ID=0x%llx\n", x3);
		break;
	}

	return spmc_ffa_error_return(handle,
			FFA_ERROR_NOT_SUPPORTED);
}

static uint64_t direct_req_smc_handler(uint64_t x1,
				       uint64_t x2,
				       uint64_t x3,
				       uint64_t x4,
				       void *cookie,
				       void *handle,
				       uint64_t flags)
{
	unsigned int ns;

	ns = is_caller_non_secure(flags);
	if (ns == SMC_FROM_SECURE) {
		assert(handle == cm_get_context(SECURE));
		return direct_req_secure_smc_handler(x1, x2, x3, x4, cookie,
						     handle, flags);
	} else {
		assert(handle == cm_get_context(NON_SECURE));
		return direct_req_non_secure_smc_handler(x1, x2, x3, x4, cookie,
							 handle, flags);
	}
}

void spmc_set_config_addr(uintptr_t soc_fw_config)
{
	spmc_manifest = (void *)soc_fw_config;
}

void *spmc_get_config_addr(void)
{
	return ((void *)spmc_manifest);
}

/*******************************************************************************
 * Initialize contexts of all Secure Partitions.
 ******************************************************************************/
int32_t spmc_setup(void)
{
	int32_t ret;

	/* Disable MMU at EL1 (initialized by BL2) */
	disable_mmu_icache_el1();

	/* Initialize context of the SP */
	INFO("Secure Partition context setup start.\n");

	ret = find_and_prepare_sp_context();
	if (ret) {
		ERROR(" Error in Secure Partition finding and context preparation.\n");
		return ret;
	}

	/* Register init function for deferred init.  */
	bl31_register_bl32_init(&sp_init);

	INFO("Secure Partition setup done.\n");

	return 0;
}

/*******************************************************************************
 * Secure Partition Manager SMC handler.
 ******************************************************************************/
uint64_t spmc_smc_handler(uint32_t smc_fid,
			  uint64_t x1,
			  uint64_t x2,
			  uint64_t x3,
			  uint64_t x4,
			  void *cookie,
			  void *handle,
			  uint64_t flags)
{
	switch (smc_fid) {
	case FFA_MSG_SEND_DIRECT_REQ_SMC64:
		return direct_req_smc_handler(x1, x2, x3, x4, cookie, handle, flags);

	case FFA_MSG_SEND_DIRECT_RESP_SMC64:
		spm_sp_synchronous_exit(&(spmc_sp_ctx[schedule_sp_index].sp_ctx), x4);

	default:
		WARN("Not Supported 0x%x FFA Request ID\n", smc_fid);
		break;
	}

	SMC_RET1(handle, SMC_UNK);
}
