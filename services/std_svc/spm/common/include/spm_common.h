/*
 * Copyright (c) 2021, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SPM_COMMON_H
#define SPM_COMMON_H

#include <context.h>

#define SP_C_RT_CTX_X19		0x0
#define SP_C_RT_CTX_X20		0x8
#define SP_C_RT_CTX_X21		0x10
#define SP_C_RT_CTX_X22		0x18
#define SP_C_RT_CTX_X23		0x20
#define SP_C_RT_CTX_X24		0x28
#define SP_C_RT_CTX_X25		0x30
#define SP_C_RT_CTX_X26		0x38
#define SP_C_RT_CTX_X27		0x40
#define SP_C_RT_CTX_X28		0x48
#define SP_C_RT_CTX_X29		0x50
#define SP_C_RT_CTX_X30		0x58

#define SP_C_RT_CTX_SIZE	0x60
#define SP_C_RT_CTX_ENTRIES	(SP_C_RT_CTX_SIZE >> DWORD_SHIFT)

#ifndef __ASSEMBLER__
#include <stdint.h>

#include <lib/spinlock.h>
#include <lib/xlat_tables/xlat_tables_v2.h>
#include <lib/utils_def.h>

/*
 * SMC IDs defined in [1] for accessing MM services from the Non-secure world.
 * These FIDs occupy the range 0x40 - 0x5f.
 * [1] DEN0060A_ARM_MM_Interface_Specification.pdf
 */
#define MM_INTERFACE_ID_AARCH64               U(0xC4000041)
#define MM_INTERFACE_ID_AARCH32               U(0x84000041)

/*
 * Macros used by SP_MEMORY_ATTRIBUTES_SET_AARCH64.
 */

#define SP_MEMORY_ATTRIBUTES_ACCESS_NOACCESS    U(0)
#define SP_MEMORY_ATTRIBUTES_ACCESS_RW          U(1)
/* Value U(2) is reserved. */
#define SP_MEMORY_ATTRIBUTES_ACCESS_RO          U(3)
#define SP_MEMORY_ATTRIBUTES_ACCESS_MASK        U(3)
#define SP_MEMORY_ATTRIBUTES_ACCESS_SHIFT       U(0)

#define SP_MEMORY_ATTRIBUTES_EXEC               (U(0) << 2)
#define SP_MEMORY_ATTRIBUTES_NON_EXEC           (U(1) << 2)


/* SPM error codes. */
#define SPM_SUCCESS		  0
#define SPM_NOT_SUPPORTED	 -1
#define SPM_INVALID_PARAMETER	 -2
#define SPM_DENIED		 -3
#define SPM_NO_MEMORY		 -5

typedef enum sp_state {
	SP_STATE_RESET = 0,
	SP_STATE_IDLE,
	SP_STATE_BUSY
} sp_state_t;

typedef struct sp_context {
	uint64_t c_rt_ctx;
	cpu_context_t cpu_ctx;
	xlat_ctx_t *xlat_ctx_handle;

	sp_state_t state;
	spinlock_t state_lock;
} sp_context_t;

/* Assembly helpers */
uint64_t spm_secure_partition_enter(uint64_t *c_rt_ctx);
void __dead2 spm_secure_partition_exit(uint64_t c_rt_ctx, uint64_t ret);

void spm_sp_setup(sp_context_t *sp_ctx);

xlat_ctx_t *spm_get_sp_xlat_context(void);

int32_t spm_memory_attributes_get_smc_handler(sp_context_t *sp_ctx,
					      uintptr_t base_va);
int spm_memory_attributes_set_smc_handler(sp_context_t *sp_ctx,
					  u_register_t page_address,
					  u_register_t pages_count,
					  u_register_t smc_attributes);

void sp_state_set(sp_context_t *sp_ptr, sp_state_t state);
void sp_state_wait_switch(sp_context_t *sp_ptr, sp_state_t from, sp_state_t to);
int sp_state_try_switch(sp_context_t *sp_ptr, sp_state_t from, sp_state_t to);
uint64_t spm_sp_synchronous_entry(sp_context_t *ctx);
__dead2 void spm_sp_synchronous_exit(sp_context_t *ctx, uint64_t rc);

#endif /* __ASSEMBLER__ */

#endif /* SPM_COMMON_H */
