/*
 * Copyright (c) 2021, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SPMC_H
#define SPMC_H

#include <stdint.h>
#include <lib/psci/psci.h>


#include "spm_common.h"

/*
 * Ranges of FF-A IDs for Normal world and Secure world components. The
 * convention matches that used by other SPMCs i.e. Hafnium and OP-TEE.
 */
#define FFA_NWLD_ID_BASE	0x0
#define FFA_NWLD_ID_LIMIT	0x7FFF
#define FFA_SWLD_ID_BASE	0x8000
#define FFA_SWLD_ID_LIMIT	0xFFFF
#define FFA_SWLD_ID_MASK	0x8000

#define FFA_HYP_ID		FFA_NWLD_ID_BASE	/* Hypervisor or physical OS is assigned 0x0 as per SMCCC */
#define FFA_SPMC_ID		U(FFA_SWLD_ID_BASE)	/* First ID is reserved for the SPMC */
#define FFA_SP_ID_BASE		(FFA_SPMC_ID + 1)	/* SP IDs are allocated after the SPMC ID */
#define INV_SP_ID		0x7FFF			/* Align with Hafnium implementation */

#define FFA_PAGE_SIZE (4096)

/*
 * Runtime states of an execution context as per the FF-A v1.1 specification.
 */
enum runtime_states {
	RT_STATE_WAITING,
	RT_STATE_RUNNING,
	RT_STATE_PREEMPTED,
	RT_STATE_BLOCKED
};

/*
 * Runtime model of an execution context as per the FF-A v1.1 specification. Its
 * value is valid only if the execution context is not in the waiting state.
 */
enum runtime_model {
	RT_MODEL_DIR_REQ,
	RT_MODEL_RUN,
	RT_MODEL_INIT,
	RT_MODEL_INTR
};

enum runtime_el {
	EL0 = 0,
	EL1,
	EL2,
	EL3
};

enum mailbox_state {
	/** There is no message in the mailbox. */
	MAILBOX_STATE_EMPTY,

	/** There is a message that has been populated in the mailbox. */
	MAILBOX_STATE_FULL,
};


struct mailbox {
	enum mailbox_state state;

	/* RX/TX Buffers */
	void *rx_buffer;
	const void *tx_buffer;

	/*
	 * Size of RX/TX Buffer
	 */
	uint32_t rxtx_page_count;

	/* Lock access to mailbox */
	struct spinlock lock;
};

/*
 * Execution context members common to both S-EL0 and S-EL1 SPs. This is a bit
 * like struct vcpu in a hypervisor.
 */
typedef struct sp_exec_ctx {
	uint64_t c_rt_ctx;
	cpu_context_t cpu_ctx;
	enum runtime_states rt_state;
	enum runtime_model rt_model;
} sp_exec_ctx_t;

/*
 * Structure to describe the cumulative properties of S-EL0 and S-EL1 SPs.
 */
typedef struct secure_partition_desc {
	/*
	 * Execution contexts allocated to this endpoint. Ideally,
	 * we need as many contexts as there are physical cpus only for a S-EL1
	 * SP which is MP-pinned. We need only a single context for a S-EL0 SP
	 * which is UP-migrateable. So, we end up wasting space when only a
	 * S-EL0 SP is deployed.
	 */
	sp_exec_ctx_t ec[PLATFORM_CORE_COUNT];

	/*
	 * ID of the Secure Partition
	 */
	uint16_t sp_id;

	/*
	 * Runtime EL
	 */
	uint16_t runtime_el;

	/*
	 * Mailbox tracking
	 */
	struct mailbox mailbox;

	/*
	 * Partition UUID
	 */
	uint32_t uuid[4];

	/*
	 * Partition Properties
	 */
	uint32_t properties;

	/*
	 * Supported FFA Version
	 */
	uint32_t ffa_version;

	/*
	 * Execution State
	 */
	uint32_t execution_state;

	/*
	 * Lock to protect the runtime state of a S-EL0 SP execution context.
	 */
	spinlock_t rt_state_lock;

	/*
	 * Pointer to translation table context of a S-EL0 SP.
	 */
	xlat_ctx_t *xlat_ctx_handle;

	/*
	 * Stack base of a S-EL0 SP.
	 */
	uint64_t   sp_stack_base;

	/*
	 * Stack size of a S-EL0 SP.
	 */
	uint64_t   sp_stack_size;

	/*
	 * Secondary entrypoint. Only valid for a S-EL1 SP.
	 */
	uintptr_t    secondary_ep;

	/*
	 * Lock to protect the secondary entrypoint update in a SP descriptor.
	 */
	spinlock_t secondary_ep_lock;
} sp_desc_t;

/*
 * This define identifies the only SP that will be initialised and participate
 * in FF-A communication. The implementation leaves the door open for more SPs
 * to be managed in future but for now it reasonable to assume that either a
 * single S-EL0 or a single S-EL1 SP will be supported. This define will be used
 * to identify which SP descriptor to initialise and manage during SP runtime.
 */
#define ACTIVE_SP_DESC_INDEX	0

/*
 * Structure to describe the cumulative properties of the Hypervisor and
 * NS-Endpoints.
 */
typedef struct ns_endpoint_desc {
	/*
	 * ID of the NS-Endpoint or Hypervisor
	 */
	uint16_t ns_ep_id;

	/*
	 * Mailbox tracking
	 */
	struct mailbox mailbox;

	/*
	 * Supported FFA Version
	 */
	uint32_t ffa_version;

} ns_ep_desc_t;

/**
 * Holds information returned for each partition by the FFA_PARTITION_INFO_GET
 * interface.
 */
struct ffa_partition_info {
	uint16_t ep_id;
	uint16_t execution_ctx_count;
	uint32_t properties;
};

/* Reference to power management hooks */
extern const spd_pm_ops_t spmc_pm;

/* Setup Function for different SP types. */
void spmc_sp_common_setup(sp_desc_t *sp, entry_point_info_t *ep_info);
void spmc_el0_sp_setup(sp_desc_t *sp, entry_point_info_t *ep_info);
void spmc_el1_sp_setup(sp_desc_t *sp, entry_point_info_t *ep_info);

/*
 * Helper function to perform a synchronous entry into a SP.
 */
uint64_t spmc_sp_synchronous_entry(sp_exec_ctx_t *ec);

/*
 * Helper function to obtain the descriptor of the current SP on a physical cpu.
 */
sp_desc_t* spmc_get_current_sp_ctx();

/*
 * Helper function to obtain the index of the execution context of an SP on a
 * physical cpu.
 */
unsigned int get_ec_index(sp_desc_t *sp);

uint64_t spmc_ffa_error_return(void *handle, int error_code);

/*
 * Helper function to obtain the RX/TX buffer pair descriptor of the Hypervisor
 * or the last SP that was run.
 */
struct mailbox *spmc_get_mbox_desc(uint64_t flags);

#endif /* SPMC_H */
