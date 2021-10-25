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
#include <bl31/interrupt_mgmt.h>
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
#include <services/spmd_svc.h>
#include <services/logical_sp.h>
#include <smccc_helpers.h>

#include <plat/arm/common/plat_arm.h>
#include <platform_def.h>

#include "spmc.h"
#include "spm_shim_private.h"
#include "spmc_shared_mem.h"

/*
 * Allocate a secure partition descriptor to describe each SP in the system that
 * does reside at EL3.
 */
static sp_desc_t sp_desc[SECURE_PARTITION_COUNT];

/*
 * Allocate an NS endpoint descriptor to describe each VM and the Hypervisor in
 * the system that interacts with a SP. It is used to track the Hypervisor
 * buffer pair, version and ID for now. It could be extended to track VM
 * properties when the SPMC supports indirect messaging.
 */
static ns_ep_desc_t ns_ep_desc[NS_PARTITION_COUNT];

static uint64_t spmc_sp_interrupt_handler(uint32_t id,
					  uint32_t flags,
					  void *handle,
					  void *cookie);

el3_lp_desc_t* get_el3_lp_array(void) {
	el3_lp_desc_t *el3_lp_descs;
	el3_lp_descs = (el3_lp_desc_t *) EL3_LP_DESCS_START;

	return el3_lp_descs;
}

/*
 * Helper function to obtain the descriptor of the last SP to whom control was
 * handed to on this physical cpu. Currently, we assume there is only one SP.
 * TODO: Expand to track multiple partitions. In this case, the last S-EL0 SP on
 * each physical cpu could be different.
 */
sp_desc_t* spmc_get_current_sp_ctx() {
	return &(sp_desc[ACTIVE_SP_DESC_INDEX]);
}

/* Helper function to get pointer to SP context from it's ID. */
sp_desc_t* spmc_get_sp_ctx(uint16_t id) {
	/* Check for Swld Partitions. */
	for (int i = 0; i < SECURE_PARTITION_COUNT; i++) {
		if (sp_desc[i].sp_id == id) {
			return &(sp_desc[i]);
		}
	}
	return NULL;
}

/*
 * Helper function to obtain the descriptor of the Hypervisor. We assume that
 * the first descriptor is reserved for the Hypervisor.
 */
ns_ep_desc_t* spmc_get_hyp_ctx() {
	return &(ns_ep_desc[0]);
}

/*
 * Helper function to obtain the RX/TX buffer pair descriptor of the Hypervisor
 * or the last SP that was run.
 */
struct mailbox *spmc_get_mbox_desc(uint64_t flags)
{
	/* Obtain the RX/TX buffer pair descriptor. */
	if (is_caller_secure(flags))
		return &(spmc_get_current_sp_ctx()->mailbox);
	else
		return &(spmc_get_hyp_ctx()->mailbox);
}

/*******************************************************************************
 * This function returns to the place where spmc_sp_synchronous_entry() was
 * called originally.
 ******************************************************************************/
__dead2 void spmc_sp_synchronous_exit(sp_exec_ctx_t *ec, uint64_t rc)
{
	/*
	 * The SPM must have initiated the original request through a
	 * synchronous entry into the secure partition. Jump back to the
	 * original C runtime context with the value of rc in x0;
	 */
	spm_secure_partition_exit(ec->c_rt_ctx, rc);

	panic();
}

/*******************************************************************************
 * Return FFA_ERROR with specified error code
 ******************************************************************************/
uint64_t spmc_ffa_error_return(void *handle, int error_code)
{
	SMC_RET8(handle, FFA_ERROR,
		 FFA_TARGET_INFO_MBZ, error_code,
		 FFA_PARAM_MBZ, FFA_PARAM_MBZ, FFA_PARAM_MBZ,
		 FFA_PARAM_MBZ, FFA_PARAM_MBZ);
}

/*******************************************************************************
 * This function returns either forwards the request to the other world or returns
 * with an ERET depending on the source of the call.
 * Assuming if call is for a logical SP it has already been taken care of.
 ******************************************************************************/
static uint64_t spmc_smc_return(uint32_t smc_fid,
				bool secure_origin,
				uint64_t x1,
				uint64_t x2,
				uint64_t x3,
				uint64_t x4,
				void *handle,
				void *cookie,
				uint64_t flags) {

	unsigned int cs;

	cs = is_caller_secure(flags);

	/* If the destination is in the normal world always go via the SPMD. */
	if (ffa_is_normal_world_id(FFA_RECEIVER(x1))) {
		return spmd_smc_handler(smc_fid, x1, x2, x3, x4, cookie, handle, flags);
	}
	/* If the caller is secure and we want to return to the secure world, ERET directly. */
	else if (cs && ffa_is_secure_world_id(FFA_RECEIVER(x1))) {
		SMC_RET5(handle, smc_fid, x1, x2, x3, x4);
	}
	/* If we originated in the normal world then switch contexts. */
	else if (!cs && ffa_is_secure_world_id(FFA_RECEIVER(x1))) {
		return ffa_smc_forward(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);
	}
	else {
		/* Unknown State. */
		panic();
	}
	/* Shouldn't be Reached */
	return 0;
}

/*******************************************************************************
 * FFA ABI Handlers
 ******************************************************************************/

bool compare_uuid(uint32_t *uuid1, uint32_t *uuid2) {
	return !memcmp(uuid1, uuid2, sizeof(uint32_t) * 4);
}

static uint64_t partition_info_get_handler(uint32_t smc_fid,
					   bool secure_origin,
					   uint64_t x1,
					   uint64_t x2,
					   uint64_t x3,
					   uint64_t x4,
					   void *cookie,
					   void *handle,
					   uint64_t flags)
{
	int index, partition_count;
	struct ffa_partition_info *info;
	el3_lp_desc_t *el3_lp_descs = get_el3_lp_array();
	struct mailbox *mbox;
	uint32_t uuid[4];
	uuid[0] = x1;
	uuid[1] = x2;
	uuid[2] = x3;
	uuid[3] = x4;

	/* Obtain the RX/TX buffer pair descriptor. */
	mbox = spmc_get_mbox_desc(flags);
	/*
	 * If the caller has not bothered registering its RX/TX pair then return
	 * the invalid parameters error code.
	 * TODO: Need a clarification in the FF-A spec for this.
	 */
	if (0 == mbox->rx_buffer) {
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);
	}

	info = (struct ffa_partition_info *) mbox->rx_buffer;

	spin_lock(&mbox->lock);
	if (mbox->state != MAILBOX_STATE_EMPTY) {
		spin_unlock(&mbox->lock);
		return spmc_ffa_error_return(handle, FFA_ERROR_BUSY);
	}
	mbox->state = MAILBOX_STATE_FULL;
	spin_unlock(&mbox->lock);

	partition_count = 0;
	/* Deal with Logical Partitions. */
	for (index = 0; index < EL3_LP_DESCS_NUM; index++) {
		if (compare_uuid(uuid, el3_lp_descs[index].uuid) ||
			(uuid[0] == 0 && uuid[1] == 0 && uuid[2] == 0 && uuid[3] == 0)) {
			/* Found a matching UUID, populate appropriately. */
			info[partition_count].ep_id = el3_lp_descs[index].sp_id;
			info[partition_count].execution_ctx_count = PLATFORM_CORE_COUNT;
			info[partition_count].properties = el3_lp_descs[index].properties;
			partition_count++;
		}
	}

	/* Deal with physical SP's. */
	for(index = 0; index < SECURE_PARTITION_COUNT; index++){
		unsigned int execution_ctx_count;
		if (compare_uuid(uuid, sp_desc[index].uuid) ||
			(uuid[0] == 0 && uuid[1] == 0 && uuid[2] == 0 && uuid[3] == 0)) {
			/* Found a matching UUID, populate appropriately. */
			info[partition_count].ep_id = sp_desc[index].sp_id;
			/* Use the EL to determine the number of execution contexts */
			execution_ctx_count = (sp_desc[index].runtime_el == EL0) ? 1: PLATFORM_CORE_COUNT;
			info[partition_count].execution_ctx_count = execution_ctx_count;
			info[partition_count].properties = sp_desc[index].properties;
			partition_count++;
		}
	}

	if (partition_count == 0) {
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);
	}

	SMC_RET3(handle, FFA_SUCCESS_SMC32, 0, partition_count);
}

static uint64_t direct_req_smc_handler(uint32_t smc_fid,
				       bool secure_origin,
				       uint64_t x1,
				       uint64_t x2,
				       uint64_t x3,
				       uint64_t x4,
				       void *cookie,
				       void *handle,
				       uint64_t flags)
{
	uint16_t dst_id = FFA_RECEIVER(x1);
	el3_lp_desc_t *el3_lp_descs;
	el3_lp_descs = get_el3_lp_array();
	sp_desc_t *sp;
	unsigned int idx;

	/* Direct request is destined for a Logical Partition. */
	for (int i = 0; i < MAX_EL3_LP_DESCS_COUNT; i++) {
		if (el3_lp_descs[i].sp_id == dst_id) {
			return el3_lp_descs[i].direct_req(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);
		}
	}

	/*
	 * If the request was not targeted to a LSP then it is invalid since a
	 * SP cannot call into the Normal world and there is no other SP to call
	 * into. If there are other SPs in future then the partition runtime
	 * model would need to be validated as well.
	 */
	if (secure_origin)
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);

	/* Check if the SP ID is valid */
	sp = spmc_get_sp_ctx(dst_id);
	if (NULL == sp)
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);

	/*
	 * Check that the target execution context is in a waiting state before
	 * forwarding the direct request to it.
	 */
	idx = get_ec_index(sp);
	if (sp->ec[idx].rt_state != RT_STATE_WAITING)
		return spmc_ffa_error_return(handle, FFA_ERROR_BUSY);

	/*
	 * Everything checks out so forward the request to the SP after updating
	 * its state and runtime model.
	 * TODO: Lock the context for a S-EL0 UP-M SP.
	 */
	sp->ec[idx].rt_state = RT_STATE_RUNNING;
	sp->ec[idx].rt_model = RT_MODEL_DIR_REQ;
	return spmc_smc_return(smc_fid, secure_origin, x1, x2, x3, x4, handle, cookie, flags);
}

static uint64_t direct_resp_smc_handler(uint32_t smc_fid,
					bool secure_origin,
					uint64_t x1,
					uint64_t x2,
					uint64_t x3,
					uint64_t x4,
					void *cookie,
					void *handle,
					uint64_t flags)
{
	sp_desc_t *sp;
	unsigned int idx;

	/* Check that the response did not originate from the Normal world */
	if (!secure_origin)
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);

	/*
	 * Check that the response is either targeted to the Normal world or the
	 * SPMC e.g. a PM response.
	 */
	if ((FFA_RECEIVER(x1) != FFA_SPMC_ID) && (FFA_RECEIVER(x1) & FFA_SWLD_ID_MASK))
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);

	/* Obtain the SP descriptor and update its runtime state */
	sp = spmc_get_sp_ctx(FFA_SENDER(x1));
	if (NULL == sp)
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);

	/* Sanity check that the state is being tracked correctly in the SPMC */
	idx = get_ec_index(sp);
	assert (sp->ec[idx].rt_state == RT_STATE_RUNNING);

	/* Ensure that the SP execution context was in the right runtime model */
	if (sp->ec[idx].rt_model != RT_MODEL_DIR_REQ)
		return spmc_ffa_error_return(handle, FFA_ERROR_DENIED);

	/* Update the state of the SP execution context */
	sp->ec[idx].rt_state = RT_STATE_WAITING;

	/*
	 * If the receiver is not the SPMC then forward the response to the
	 * Normal world.
	 */
	if (FFA_RECEIVER(x1) == FFA_SPMC_ID) {
		spmc_sp_synchronous_exit(&sp->ec[idx], x4);
		/* Should not get here */
		panic();
	}

	return spmc_smc_return(smc_fid, secure_origin, x1, x2, x3, x4, handle, cookie, flags);
}

static uint64_t rxtx_map_handler(uint32_t smc_fid,
				 bool secure_origin,
				 uint64_t x1,
				 uint64_t x2,
				 uint64_t x3,
				 uint64_t x4,
				 void *cookie,
				 void *handle,
				 uint64_t flags)
{
	int ret;
	uint32_t error_code;
	uint32_t mem_atts = secure_origin ? MT_SECURE : MT_NS;
	struct mailbox *mbox;

	uintptr_t tx_address = x1;
	uintptr_t rx_address = x2;
	uint32_t page_count = (uint32_t) x3 & 0x1F; /* Bits [5:0] */
	uint32_t buf_size = page_count * FFA_PAGE_SIZE;

	/*
	 * The SPMC does not support mapping of VM RX/TX pairs to facilitate
	 * indirect messaging with SPs. Check if the Hypervisor has invoked this
	 * ABI on behalf of a VM and reject it if this is the case.
	 * TODO: Check FF-A spec guidance on this scenario.
	 */
	if (tx_address == 0 || rx_address == 0 ) {
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);
	}

	/* Obtain the RX/TX buffer pair descriptor. */
	mbox = spmc_get_mbox_desc(flags);

	spin_lock(&mbox->lock);

	/* Check if buffers have already been mapped. */
	if (mbox->rx_buffer != 0 || mbox->tx_buffer != 0) {
		WARN("RX/TX Buffers already mapped (%p/%p)\n", (void *) mbox->rx_buffer, (void *)mbox->tx_buffer);
		error_code = FFA_ERROR_DENIED;
		goto err;
	}

	/* memmap the TX buffer as read only. */
	ret = mmap_add_dynamic_region(tx_address, /* PA */
			tx_address, /* VA */
			buf_size, /* size */
			mem_atts | MT_RO_DATA); /* attrs */
	if (ret) {
		/* Return the correct error code. */
		error_code = (ret == -ENOMEM) ? FFA_ERROR_NO_MEMORY : FFA_ERROR_INVALID_PARAMETER;
		WARN("Unable to map TX buffer: %d\n", error_code);
		mbox->rxtx_page_count = 0;
		goto err;
	}
	mbox->tx_buffer = (void *) tx_address;

	/* memmap the RX buffer as read write. */
	ret = mmap_add_dynamic_region(rx_address, /* PA */
			rx_address, /* VA */
			buf_size, /* size */
			mem_atts | MT_RW_DATA); /* attrs */

	if (ret) {
		error_code = (ret == -ENOMEM) ? FFA_ERROR_NO_MEMORY : FFA_ERROR_INVALID_PARAMETER;
		WARN("Unable to map RX buffer: %d\n", error_code);
		goto err_unmap;
	}
	mbox->rx_buffer = (void *) rx_address;
	mbox->rxtx_page_count = page_count;
	spin_unlock(&mbox->lock);

	SMC_RET1(handle, FFA_SUCCESS_SMC32);

err_unmap:
	/* Unmap the TX buffer again. */
	mmap_remove_dynamic_region(tx_address, buf_size);
err:
	mbox->tx_buffer = 0;
	spin_unlock(&mbox->lock);

	return spmc_ffa_error_return(handle, error_code);
}

static uint64_t rxtx_unmap_handler(uint32_t smc_fid,
				   bool secure_origin,
				   uint64_t x1,
				   uint64_t x2,
				   uint64_t x3,
				   uint64_t x4,
				   void *cookie,
				   void *handle,
				   uint64_t flags)
{
	struct mailbox *mbox = spmc_get_mbox_desc(flags);
	uint32_t buf_size = mbox->rxtx_page_count * FFA_PAGE_SIZE;

	/*
	 * The SPMC does not support mapping of VM RX/TX pairs to facilitate
	 * indirect messaging with SPs. Check if the Hypervisor has invoked this
	 * ABI on behalf of a VM and reject it if this is the case.
	 * TODO: Check FF-A spec guidance on this scenario.
	 */
	if (x1 != 0) {
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);
	}

	spin_lock(&mbox->lock);

	/* Check if buffers have already been mapped. */
	if (mbox->rx_buffer == 0 || mbox->tx_buffer == 0) {
		spin_unlock(&mbox->lock);
		return spmc_ffa_error_return(handle, FFA_ERROR_DENIED);
	}

	/* unmap RX Buffer */
	mmap_remove_dynamic_region((uintptr_t) mbox->rx_buffer, buf_size);
	mbox->rx_buffer = 0;

	/* unmap TX Buffer */
	mmap_remove_dynamic_region((uintptr_t) mbox->tx_buffer, buf_size);
	mbox->tx_buffer = 0;

	spin_unlock(&mbox->lock);
	SMC_RET1(handle, FFA_SUCCESS_SMC32);
}

static uint64_t ffa_features_handler(uint32_t smc_fid,
				     bool secure_origin,
				     uint64_t x1,
				     uint64_t x2,
				     uint64_t x3,
				     uint64_t x4,
				     void *cookie,
				     void *handle,
				     uint64_t flags)
{
	uint32_t function_id = x1;

	if (function_id & FFA_VERSION_BIT31_MASK) {

		switch (function_id) {
		case FFA_ERROR:
		case FFA_SUCCESS_SMC32:
		case FFA_SUCCESS_SMC64:
		case FFA_SPM_ID_GET:
		case FFA_ID_GET:
		case FFA_FEATURES:
		case FFA_VERSION:
		case FFA_RX_RELEASE:
		case FFA_MSG_SEND_DIRECT_REQ_SMC32:
		case FFA_MSG_SEND_DIRECT_REQ_SMC64:
		case FFA_MSG_SEND_DIRECT_RESP_SMC32:
		case FFA_MSG_SEND_DIRECT_RESP_SMC64:
		case FFA_PARTITION_INFO_GET:
		case FFA_RXTX_MAP_SMC64:
		case FFA_RXTX_UNMAP:
		case FFA_MEM_SHARE_SMC64:
		case FFA_MEM_LEND_SMC64:
		case FFA_MEM_FRAG_TX:
		case FFA_MEM_FRAG_RX:
		case FFA_MEM_RETRIEVE_REQ_SMC32:
		case FFA_MEM_RETRIEVE_REQ_SMC64:
		case FFA_MEM_RELINQUISH:
		case FFA_MEM_RECLAIM:
		case FFA_MSG_RUN:
		case FFA_MSG_WAIT:

			SMC_RET1(handle, FFA_SUCCESS_SMC64);

		default:
			return spmc_ffa_error_return(handle, FFA_ERROR_NOT_SUPPORTED);
		}
	}
	else{
		/* TODO: Handle features. */
		return spmc_ffa_error_return(handle, FFA_ERROR_NOT_SUPPORTED);
	}
	return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);
}

static uint64_t ffa_version_handler(uint32_t smc_fid,
						bool secure_origin,
						uint64_t x1,
						uint64_t x2,
						uint64_t x3,
						uint64_t x4,
						void *cookie,
						void *handle,
						uint64_t flags)
{
	/*
	 * Ensure that both major and minor revision representation occupies at
	 * most 15 bits.
	 */
	assert(0x8000 > FFA_VERSION_MAJOR);
	assert(0x10000 > FFA_VERSION_MINOR);

	if (x1 & FFA_VERSION_BIT31_MASK) {
		/* Invalid encoding, return an error. */
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);
	}

	SMC_RET1(handle,
		 FFA_VERSION_MAJOR << FFA_VERSION_MAJOR_SHIFT |
		 FFA_VERSION_MINOR);
}

static uint64_t ffa_id_get_handler(uint32_t smc_fid,
				   bool secure_origin,
				   uint64_t x1,
				   uint64_t x2,
				   uint64_t x3,
				   uint64_t x4,
				   void *cookie,
				   void *handle,
				   uint64_t flags)
{
	if (is_caller_secure(flags)) {
		SMC_RET3(handle, FFA_SUCCESS_SMC32, 0x0, spmc_get_current_sp_ctx()->sp_id);
	} else {
		SMC_RET3(handle, FFA_SUCCESS_SMC32, 0x0, spmc_get_hyp_ctx()->ns_ep_id);
	}
}

static uint64_t ffa_spm_id_get_handler(uint32_t smc_fid,
				       bool secure_origin,
				       uint64_t x1,
				       uint64_t x2,
				       uint64_t x3,
				       uint64_t x4,
				       void *cookie,
				       void *handle,
				       uint64_t flags)
{
	if (is_caller_secure(flags)) {
		SMC_RET3(handle, FFA_SUCCESS_SMC32, 0x0, FFA_SPMC_ID);
	} else {
		return spmc_ffa_error_return(handle, FFA_ERROR_DENIED);
	}
}

static uint64_t ffa_run_handler(uint32_t smc_fid,
				bool secure_origin,
				uint64_t x1,
				uint64_t x2,
				uint64_t x3,
				uint64_t x4,
				void *cookie,
				void *handle,
				uint64_t flags)
{
	sp_desc_t *sp;
	unsigned int idx, *rt_state, *rt_model;

	/* Can only be called from the normal world. */
	if (secure_origin) {
		spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);
	}

	/* Cannot run a Normal world partition. */
	if (!(FFA_RUN_TARGET(x1) & FFA_SWLD_ID_MASK)) {
		spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);
	}

	/*
	 * Check that the context is not already running on a different
	 * cpu. This is valid only for a S-EL SP.
	 */
	sp = spmc_get_sp_ctx(FFA_RUN_TARGET(x1));
	if (NULL == sp)
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);

	idx = get_ec_index(sp);
	rt_state = &sp->ec[idx].rt_state;
	rt_model = &sp->ec[idx].rt_model;
	if (*rt_state == RT_STATE_RUNNING)
		return spmc_ffa_error_return(handle, FFA_ERROR_BUSY);

	/*
	 * Sanity check that if the execution context was not waiting then it
	 * was either in the direct request or the run partition runtime model.
	 */
	if (*rt_state == RT_STATE_PREEMPTED || *rt_state == RT_STATE_BLOCKED)
		assert(*rt_model == RT_MODEL_RUN || *rt_model == RT_MODEL_DIR_REQ);

	/*
	 * If the context was waiting then update the partition runtime model.
	 */
	if (*rt_state == RT_STATE_WAITING)
		*rt_model = RT_MODEL_RUN;

	/*
	 * Forward the request to the correct SP vCPU after updating
	 * its state.
	 * TODO: Lock the context in case of a S-EL0 UP-M SP.
	 */
	*rt_state = RT_STATE_RUNNING;

	return spmc_smc_return(smc_fid, secure_origin, FFA_RUN_TARGET(x1), 0, 0, 0, handle, cookie, flags);
}

static uint64_t msg_wait_handler(uint32_t smc_fid,
				 bool secure_origin,
				 uint64_t x1,
				 uint64_t x2,
				 uint64_t x3,
				 uint64_t x4,
				 void *cookie,
				 void *handle,
				 uint64_t flags)
{
	sp_desc_t *sp;
	unsigned int idx;

	/* Check that the response did not originate from the Normal world */
	if (!secure_origin)
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);

	/*
	 * Get the descriptor of the SP that invoked FFA_MSG_WAIT.
	 */
	sp = spmc_get_current_sp_ctx();
	if (NULL == sp)
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);

	/*
	 * Get the execution context of the SP that invoked FFA_MSG_WAIT.
	 */
	idx = get_ec_index(sp);

	/* Ensure that the SP execution context was in the right runtime model */
	if (sp->ec[idx].rt_model == RT_MODEL_DIR_REQ)
		return spmc_ffa_error_return(handle, FFA_ERROR_DENIED);

	/* Sanity check that the state is being tracked correctly in the SPMC */
	idx = get_ec_index(sp);
	assert (sp->ec[idx].rt_state == RT_STATE_RUNNING);

	/*
	 * Perform a synchronous exit if the partition was initialising. The
	 * state is updated after the exit.
	 */
	if (sp->ec[idx].rt_model == RT_MODEL_INIT) {
		spmc_sp_synchronous_exit(&sp->ec[idx], x4);
		/* Should not get here */
		panic();
	}

	/* Update the state of the SP execution context */
	sp->ec[idx].rt_state = RT_STATE_WAITING;

	/* Resume normal world if a secure interrupt was handled */
	if (sp->ec[idx].rt_model == RT_MODEL_INTR) {
		unsigned int secure_state_in = (secure_origin) ? SECURE : NON_SECURE;
		unsigned int secure_state_out = (!secure_origin) ? SECURE : NON_SECURE;

		assert(secure_state_in == SECURE);
		assert(secure_state_out == NON_SECURE);

		cm_el1_sysregs_context_save(secure_state_in);
		cm_el1_sysregs_context_restore(secure_state_out);
		cm_set_next_eret_context(secure_state_out);
		SMC_RET0(cm_get_context(secure_state_out));
	}
	/* Forward the response to the Normal world */
	return spmc_smc_return(smc_fid, secure_origin, x1, x2, x3, x4, handle, cookie, flags);
}

static uint64_t rx_release_handler(uint32_t smc_fid,
				   bool secure_origin,
				   uint64_t x1,
				   uint64_t x2,
				   uint64_t x3,
				   uint64_t x4,
				   void *cookie,
				   void *handle,
				   uint64_t flags)
{	struct mailbox *mbox = spmc_get_mbox_desc(flags);
	spin_lock(&mbox->lock);

	if (mbox->state != MAILBOX_STATE_FULL) {
		spin_unlock(&mbox->lock);
		return spmc_ffa_error_return(handle, FFA_ERROR_DENIED);
	}
	mbox->state = MAILBOX_STATE_EMPTY;
	spin_unlock(&mbox->lock);

	SMC_RET1(handle, FFA_SUCCESS_SMC32);
}

/*******************************************************************************
 * spmc_pm_secondary_ep_register
 ******************************************************************************/
static uint64_t ffa_sec_ep_register_handler(uint32_t smc_fid,
					    bool secure_origin,
					    uint64_t x1,
					    uint64_t x2,
					    uint64_t x3,
					    uint64_t x4,
					    void *cookie,
					    void *handle,
					    uint64_t flags)
{
	sp_desc_t *sp;

	/*
	 * This request cannot originate from the Normal world.
	 */
	if (!secure_origin)
		return spmc_ffa_error_return(handle, FFA_ERROR_NOT_SUPPORTED);

	/* Get the context of the current SP. */
	sp = spmc_get_current_sp_ctx();
	if (NULL == sp)
		return spmc_ffa_error_return(handle, FFA_ERROR_INVALID_PARAMETER);

	/*
	 * A S-EL0 SP has no business invoking this ABI.
	 */
	if (sp->runtime_el == EL0) {
		return spmc_ffa_error_return(handle, FFA_ERROR_DENIED);
	}

	/*
	 * Lock and update the secondary entrypoint in SP context.
	 * TODO: Sanity check the entrypoint even though it does not matter
	 * since there is no isolation between EL3 and S-EL1.
	 */
	spin_lock(&sp->secondary_ep_lock);
	sp->secondary_ep = x1;
	VERBOSE("%s %lx\n", __func__, sp->secondary_ep);
	spin_unlock(&sp->secondary_ep_lock);

	SMC_RET1(handle, FFA_SUCCESS_SMC32);
}

/*******************************************************************************
 * This function will parse the Secure Partition Manifest for fetching seccure
 * partition specific memory region details. It will find base address, size,
 * memory attributes for each memory region and then add the respective region
 * into secure parition's translation context.
 ******************************************************************************/
static void populate_sp_mem_regions(sp_desc_t *sp,
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
			mmap_add_region_ctx(sp->xlat_ctx_handle,
					    &sp_mem_regions);
		}
	}
}

/*
 * Convert from the traditional TF-A representation of a UUID,
 * big endian uint8 to little endian uint32 to be inline
 * with FF-A.
 */
void convert_uuid_endian(uint8_t *be_8, uint32_t *le_32) {
	for (int i = 0; i < 4; i++){
		le_32[i] = be_8[(i*4)+0] << 24 |
				   be_8[(i*4)+1] << 16 |
				   be_8[(i*4)+2] << 8  |
				   be_8[(i*4)+3] << 0;
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
			     sp_desc_t *sp,
			     entry_point_info_t *ep_info)
{
	int32_t ret, node;
	uint64_t config;
	uint32_t config_32;
	uint8_t be_uuid[16];

	/*
	 * Look for the mandatory fields that are expected to be present in
	 * both S-EL1 and S-EL0 SP manifests.
	 */
	node = fdt_subnode_offset_namelen(sp_manifest, offset,
					  "ffa-config",
					  sizeof("ffa-config") - 1);
	if (node < 0) {
		ERROR("Not found any ffa-config for SP.\n");
		return node;
	}

	ret = fdt_read_uint32(sp_manifest, node,
			      "runtime-el", &config_32);
	if (ret) {
		ERROR("Missing SP Runtime EL information.\n");
		return ret;
	} else
		sp->runtime_el = config_32;

	ret = fdtw_read_uuid(sp_manifest, node, "uuid", 16,
			     be_uuid);
	if (ret) {
		ERROR("Missing Secure Partition UUID.\n");
		return ret;
	} else {
		/* Convert from BE to LE to store internally. */
		convert_uuid_endian(be_uuid, sp->uuid);
	}

	ret = fdt_read_uint32(sp_manifest, node,
			      "ffa-version", &config_32);
	if (ret) {
		ERROR("Missing Secure Partition FFA Version.\n");
		return ret;
	} else
		sp->ffa_version = config_32;

	ret = fdt_read_uint32(sp_manifest, node,
			      "execution-state", &config_32);
	if (ret) {
		ERROR("Missing Secure Partition Execution State.\n");
		return ret;
	} else
		sp->execution_state = config_32;

	/*
	 * Look for the optional fields that are expected to be present in
	 * both S-EL1 and S-EL0 SP manifests.
	 */

	ret = fdt_read_uint32(sp_manifest, node,
			      "partition_id", &config_32);
	if (ret)
		WARN("Missing Secure Partition ID.\n");
	else
		sp->sp_id = config_32;

	ret = fdt_read_uint64(sp_manifest, node,
			      "load_address", &config);
	if (ret)
		WARN("Missing Secure Partition Entry Point.\n");
	else
		ep_info->pc = config;

	/*
	 * Look for the mandatory fields that are expected to be present in only
	 * a StMM S-EL0 SP manifest. We are assuming deployment of only a single
	 * StMM SP with the EL3 SPMC for now.
	 */
	if (sp->runtime_el == EL0) {
		ret = fdt_read_uint64(sp_manifest, node,
				      "sp_arg0", &config);
		if (ret) {
			ERROR("Missing Secure Partition arg0.\n");
			return ret;
		} else
			ep_info->args.arg0 = config;

		ret = fdt_read_uint64(sp_manifest, node,
				      "sp_arg1", &config);
		if (ret) {
			ERROR("Missing Secure Partition  arg1.\n");
			return ret;
		} else
			ep_info->args.arg1 = config;

		ret = fdt_read_uint64(sp_manifest, node,
				      "sp_arg2", &config);
		if (ret) {
			ERROR("Missing Secure Partition  arg2.\n");
			return ret;
		} else
			ep_info->args.arg2 = config;

		ret = fdt_read_uint64(sp_manifest, node,
				      "sp_arg3", &config);
		if (ret) {
			ERROR("Missing Secure Partition  arg3.\n");
			return ret;
		} else
			ep_info->args.arg3 = config;

		ret = fdt_read_uint64(sp_manifest, node,
				      "stack_base", &config);
		if (ret) {
			ERROR("Missing Secure Partition Stack Base.\n");
			return ret;
		} else
			sp->sp_stack_base = config;

		ret = fdt_read_uint64(sp_manifest, node,
				      "stack_size", &config);
		if (ret) {
			ERROR("Missing Secure Partition Stack Size.\n");
			return ret;
		} else
			sp->sp_stack_size = config;
	}

	node = fdt_subnode_offset_namelen(sp_manifest, offset,
					  "mem-regions",
					  sizeof("mem-regions") - 1);
	if (node < 0)
		WARN("Not found mem-region configuration for SP.\n");
	else {
		populate_sp_mem_regions(sp, sp_manifest, node);
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
	sp_desc_t *sp;
	entry_point_info_t ep_info = {0};

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
	}

	/*
	 * Allocate an SP descriptor for initialising the partition's execution
	 * context on the primary CPU.
	 */
	sp = &(sp_desc[ACTIVE_SP_DESC_INDEX]);

	/* Assign translation tables context. */
	sp_desc->xlat_ctx_handle = spm_get_sp_xlat_context();

	/* Initialize entry point information for the SP */
	SET_PARAM_HEAD(&ep_info, PARAM_EP, VERSION_1, SECURE | EP_ST_ENABLE);

	/* Parse the SP manifest. */
	ret = sp_manifest_parse(sp_manifest, ret, sp, &ep_info);
	if (ret) {
		ERROR(" Error in Secure Partition(SP) manifest parsing.\n");
		return ret;
	}

	/* Check that the runtime EL in the manifest was correct */
	if (sp->runtime_el != EL0 && sp->runtime_el != EL1) {
		ERROR("Unexpected runtime EL: %d\n", sp->runtime_el);
		return -EINVAL;
	}

	/* Perform any initialisation common to S-EL0 and S-EL1 SP */
	spmc_sp_common_setup(sp, &ep_info);

	/* Perform any initialisation specific to S-EL0 or S-EL1 SP */
	if (sp->runtime_el == 0)
		spmc_el0_sp_setup(sp, &ep_info);
	else
		spmc_el1_sp_setup(sp, &ep_info);

	return 0;
}

/*******************************************************************************
 * This function takes an SP context pointer and performs a synchronous entry
 * into it.
 ******************************************************************************/
uint64_t spmc_sp_synchronous_entry(sp_exec_ctx_t *ec)
{
	uint64_t rc;

	assert(ec != NULL);

	/* Assign the context of the SP to this CPU */
	cm_set_context(&(ec->cpu_ctx), SECURE);

	/* Restore the context assigned above */
	cm_el1_sysregs_context_restore(SECURE);
	cm_set_next_eret_context(SECURE);

	/* Invalidate TLBs at EL1. */
	tlbivmalle1();
	dsbish();

	/* Enter Secure Partition */
	rc = spm_secure_partition_enter(&ec->c_rt_ctx);

	/* Save secure state */
	cm_el1_sysregs_context_save(SECURE);

	return rc;
}

static int32_t logical_sp_init(void)
{
	uint64_t rc = 0;

	el3_lp_desc_t *el3_lp_descs;
	el3_lp_descs = get_el3_lp_array();

	INFO("Logical Secure Partition init start.\n");
	/* TODO: do some initialistion. */
	for (int i = 0; i < EL3_LP_DESCS_NUM; i++) {
		el3_lp_descs[i].init();
	}

	INFO("Secure Partition initialized.\n");

	return rc;
}

/*******************************************************************************
 * SPMC Helper Functions
 ******************************************************************************/
static int32_t sp_init(void)
{
	uint64_t rc;
	sp_desc_t *sp;
	sp_exec_ctx_t *ec;

	sp = &(sp_desc[ACTIVE_SP_DESC_INDEX]);
	ec = &sp->ec[get_ec_index(sp)];
	ec->rt_model = RT_MODEL_INIT;
	ec->rt_state = RT_STATE_RUNNING;

	INFO("Secure Partition (0x%x) init start.\n", sp->sp_id);

	rc = spmc_sp_synchronous_entry(ec);
	assert(rc == 0);

	ERROR("S-EL1 SP context on core%u is in %u state\n", get_ec_index(sp), ec->rt_state);
	ec->rt_state = RT_STATE_WAITING;
	ERROR("S-EL1 SP context on core%u is in %u state\n", get_ec_index(sp), ec->rt_state);

	INFO("Secure Partition initialized.\n");

	return !rc;
}

static void initalize_sp_descs(void) {
	sp_desc_t *sp;
	for (int i = 0; i < SECURE_PARTITION_COUNT; i++) {
		sp = &sp_desc[i];
		sp->sp_id = INV_SP_ID;
		sp->mailbox.rx_buffer = 0;
		sp->mailbox.tx_buffer = 0;
		sp->mailbox.state = MAILBOX_STATE_EMPTY;
		sp->secondary_ep = 0;
	}
}

static void initalize_ns_ep_descs(void) {
	ns_ep_desc_t *ns_ep;
	for (int i = 0; i < NS_PARTITION_COUNT; i++) {
		ns_ep = &ns_ep_desc[i];
		/* Clashes with the Hypervisor ID but wil not be a problem in practice */
		ns_ep->ns_ep_id = 0;
		ns_ep->mailbox.rx_buffer = 0;
		ns_ep->mailbox.tx_buffer = 0;
		ns_ep->mailbox.state = MAILBOX_STATE_EMPTY;
	}
}

/*******************************************************************************
 * Initialize contexts of all Secure Partitions.
 ******************************************************************************/
int32_t spmc_setup(void)
{
	int32_t ret;
	uint32_t flags;

	/* Initialize endpoint descriptors */
	initalize_sp_descs();
	initalize_ns_ep_descs();

	/* Setup logical SPs. */
	logical_sp_init();

	/* Perform physical SP setup. */

	/* Disable MMU at EL1 (initialized by BL2) */
	disable_mmu_icache_el1();

	/* Initialize context of the SP */
	INFO("Secure Partition context setup start.\n");

	ret = find_and_prepare_sp_context();
	if (ret) {
		ERROR(" Error in Secure Partition finding and context preparation.\n");
		return ret;
	}

	/* Register power management hooks with PSCI */
	psci_register_spd_pm_hook(&spmc_pm);

	/*
	 * Register an interrupt handler for S-EL1 interrupts
	 * when generated during code executing in the
	 * non-secure state.
	 */
	flags = 0;
	set_interrupt_rm_flag(flags, NON_SECURE);
	ret = register_interrupt_type_handler(INTR_TYPE_S_EL1,
					      spmc_sp_interrupt_handler,
					      flags);
	if (ret)
		panic();

	/* Register init function for deferred init.  */
	bl31_register_bl32_init(&sp_init);

	INFO("Secure Partition setup done.\n");

	return 0;
}

/*******************************************************************************
 * Secure Partition Manager SMC handler.
 ******************************************************************************/
uint64_t spmc_smc_handler(uint32_t smc_fid,
			  bool secure_origin,
			  uint64_t x1,
			  uint64_t x2,
			  uint64_t x3,
			  uint64_t x4,
			  void *cookie,
			  void *handle,
			  uint64_t flags)
{
	VERBOSE("SPMC: 0x%x 0x%llx 0x%llx 0x%llx 0x%llx\n", smc_fid, x1, x2, x3, x4);
	switch (smc_fid) {
	case FFA_SPM_ID_GET:
		return ffa_spm_id_get_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);
	case FFA_ID_GET:
		return ffa_id_get_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);
	case FFA_FEATURES:
		return ffa_features_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);
	case FFA_VERSION:
		return ffa_version_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_SECONDARY_EP_REGISTER_SMC64:
		return ffa_sec_ep_register_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_MSG_SEND_DIRECT_REQ_SMC32:
	case FFA_MSG_SEND_DIRECT_REQ_SMC64:
		return direct_req_smc_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_MSG_SEND_DIRECT_RESP_SMC32:
	case FFA_MSG_SEND_DIRECT_RESP_SMC64:
		return direct_resp_smc_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_PARTITION_INFO_GET:
		return partition_info_get_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_RXTX_MAP_SMC32:
	case FFA_RXTX_MAP_SMC64:
		return rxtx_map_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_RXTX_UNMAP:
		return rxtx_unmap_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_RX_RELEASE:
		return rx_release_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_MSG_WAIT:
		/*
		 * Normal world cannot call this into the Secure world.
		 */
		if (!secure_origin)
			break;

		return msg_wait_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_MSG_RUN:
		return ffa_run_handler(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_MEM_SHARE_SMC64:
	case FFA_MEM_LEND_SMC64:
		return spmc_ffa_mem_send(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_MEM_FRAG_TX:
		return spmc_ffa_mem_frag_tx(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_MEM_FRAG_RX:
		return spmc_ffa_mem_frag_rx(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_MEM_RETRIEVE_REQ_SMC32:
	case FFA_MEM_RETRIEVE_REQ_SMC64:
		return spmc_ffa_mem_retrieve_req(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_MEM_RELINQUISH:
		return spmc_ffa_mem_relinquish(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	case FFA_MEM_RECLAIM:
		return spmc_ffa_mem_reclaim(smc_fid, secure_origin, x1, x2, x3, x4, cookie, handle, flags);

	default:
		WARN("Not Supported 0x%x (0x%llx, 0x%llx, 0x%llx, 0x%llx) FFA Request ID\n", smc_fid, x1, x2, x3, x4);
		break;
	}
	return spmc_ffa_error_return(handle, FFA_ERROR_NOT_SUPPORTED);
}

/*******************************************************************************
 * This function is the handler registered for S-EL1 interrupts by the SPMC. It
 * validates the interrupt and upon success arranges entry into the SP for
 * handling the interrupt.
 ******************************************************************************/
static uint64_t spmc_sp_interrupt_handler(uint32_t id,
					  uint32_t flags,
					  void *handle,
					  void *cookie)
{
	sp_desc_t *sp = spmc_get_current_sp_ctx();
	sp_exec_ctx_t *ec;
	uint32_t linear_id = plat_my_core_pos();

	/* Sanity check for a NULL pointer dereference */
	assert (NULL != sp);

	/* Panic in case of a S-EL0 SP */
	if (sp->runtime_el == EL0) {
		ERROR("Yikes! Interrupt received for a S-EL0 SP on core%u \n", linear_id);
		panic();
	}

	/* Obtain a reference to the SP execution context */
	ec = &sp->ec[get_ec_index(sp)];

	/* Ensure that the execution context is in a waiting state else panic. */
	if (ec->rt_state != RT_STATE_WAITING) {
		ERROR("Yikes! S-EL1 SP context on core%u is in %u state\n", linear_id,
		      ec->rt_state);
		panic();
	}

	/* Update the runtime model and state of the partition */
	ec->rt_model = RT_MODEL_INTR;
	ec->rt_state = RT_STATE_RUNNING;

	VERBOSE("SP (0x%x) interrupt start on core%u \n", sp->sp_id, linear_id);

	/*
	 * Forward the interrupt to the S-EL1 SP. The interrupt ID is not
	 * populated as the SP can determine this by itself.
	 * TODO: Add support for handing interrupts to a S-EL0 SP.
	 */
	return ffa_smc_forward(FFA_INTERRUPT, is_caller_secure(flags), FFA_PARAM_MBZ,
			       FFA_PARAM_MBZ, FFA_PARAM_MBZ, FFA_PARAM_MBZ,
			       cookie, handle, flags);
}
