#include <common/debug.h>
#include <smccc_helpers.h>
#include <services/ffa_svc.h>
#include <services/logical_sp.h>

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
#include <services/spmd_svc.h>
#include <services/logical_sp.h>


#include <plat/arm/common/plat_arm.h>
#include <platform_def.h>


#define LP_PARTITION_ID 0xC001
#define LP_UUID {0x0, 0x0, 0x0, 0x2}

static int64_t sp_init(void) {
        // TODO: Do some initialisation.
        INFO("LSP: Init function called.\n");
        return 0;
}


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
 * Function to perform a call to a Secure Partition.
 * TODO: Update to unwind approach if required.
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



static uint64_t handle_ffa_direct_request(uint32_t smc_fid,  bool secure_origin, uint64_t x1, uint64_t x2,
								uint64_t x3, uint64_t x4, void *cookie, void *handle, uint64_t flags) {
        uint64_t ret;

        if (secure_origin) {
            assert(handle == cm_get_context(SECURE));
            return direct_req_secure_smc_handler(x1, x2, x3, x4, cookie,
                                handle, flags);
        } else {
            assert(handle == cm_get_context(NON_SECURE));
            return direct_req_non_secure_smc_handler(x1, x2, x3, x4, cookie,
                                handle, flags);
        }
}

/* Register logical partition  */
DECLARE_LOGICAL_PARTITION(
        stmm_lsp,
        sp_init,                   // Init Function
        LP_PARTITION_ID,           // FFA Partition ID
        LP_UUID,                   // UUID
        0x1,                       // Partition Properties. Can only receive direct messages.
        handle_ffa_direct_request  // Callback for direct requests.
);
