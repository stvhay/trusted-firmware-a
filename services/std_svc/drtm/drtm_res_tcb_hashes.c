/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * DRTM resource: TCB hashes.
 *
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>

#include <common/runtime_svc.h>
#include <services/drtm_svc_plat.h>

#include "drtm_measurements.h"  /* DRTM_TPM_HASH_ALG and _DSIZE */
#include "drtm_remediation.h"


struct __packed drtm_tcb_hash_v1 {
	uint32_t hash_id;
	uint8_t hash_val[DRTM_TPM_HASH_ALG_DSIZE];
};

struct __packed drtm_tcb_hash_table_hdr_v1 {
	uint16_t version;        /* Must be 1. */
	uint16_t num_hashes;
	uint32_t hashing_alg;
};

/* Version-agnostic types. */
typedef struct drtm_tcb_hash_table_hdr_v1 struct_drtm_tcb_hash_table_hdr;
typedef struct drtm_tcb_hash_v1 struct_drtm_tcb_hash;

CASSERT(sizeof(((struct plat_drtm_tcb_hash *)NULL)->hash_val)
		== sizeof(((struct_drtm_tcb_hash *)NULL)->hash_val),
		bad_plat_drtm_tcb_digest_buffer_size
);


static bool tcb_hashes_set_at_runtime;
static bool tcb_hashes_locked;


/* Default platform's DRTM TCB hashes enumeration -- no hashes. */
void plat_enumerate_drtm_tcb_hashes(const struct plat_drtm_tcb_hash **hashes_out,
                                    size_t *hashes_count_out)
{
	*hashes_out = NULL;
	*hashes_count_out = 0;
}
#pragma weak  plat_enumerate_drtm_tcb_hashes


int drtm_tcb_hashes_init(void)
{
	const struct plat_drtm_tcb_hash *init_hashes;
	size_t num_init_hashes;
	bool init_hashes_invalid = false;

	plat_enumerate_drtm_tcb_hashes(&init_hashes, &num_init_hashes);
	if (!init_hashes) {
		return 0;
	}

	/* Validate the platform DRTM TCB hashes. */
	for (size_t j = 0; j < num_init_hashes; j++) {
		const struct plat_drtm_tcb_hash *plat_h = init_hashes + j;

		if (plat_h->hash_bytes != DRTM_TPM_HASH_ALG_DSIZE) {
			ERROR("DRTM: invalid hash value size of platform TCB hash"
			      " at index %ld\n", j);
			init_hashes_invalid = true;
		}


		for (size_t i = 0; i < j; i++) {
			const struct plat_drtm_tcb_hash *prev_h = init_hashes + i;

			if (plat_h->hash_id.uint32 == prev_h->hash_id.uint32) {
				ERROR("DRTM: duplicate hash value ID of platform TCB hash"
				      " at index %ld (duplicates ID at index %ld)\n", j, i);
				init_hashes_invalid = true;
			}
		}
	}
	if (init_hashes_invalid) {
		return -EINVAL;
	}

	return 0;
}

uint64_t drtm_features_tcb_hashes(void *ctx)
{
	SMC_RET2(ctx, 1,            /* TCB hashes supported. */
	         (uint64_t)0 << 8   /* MBZ */
	         | (uint8_t)0       /* TCB hashes may not be recorded at runtime. */
	);
}

void drtm_dl_ensure_tcb_hashes_are_final(void)
{
	if (!tcb_hashes_set_at_runtime || tcb_hashes_locked) {
		return;
	}

	/*
	 * Some runtime TCB hashes were set, but the set of TCB hashes hasn't been
	 * locked / frozen by trusted Normal World firmware.  Therefore there is no
	 * way to guarantee that the set of TCB hashes doesn't contain malicious
	 * ones from an untrusted Normal World component.
	 * Refuse to complete the dynamic launch, and reboot the system.
	 */
	drtm_enter_remediation(0x4, "TCB hashes are still open (missing LOCK call)");
}

/*
 * enum drtm_retc drtm_set_tcb_hash(uint64_t x1)
 * {
 * 	// Sets `tcb_hashes_set_at_runtime' when it succeeds
 * }
 */

/*
 * enum drtm_retc drtm_lock_tcb_hashes(void)
 * {
 * 	// Sets `tcb_hashes_locked' when it succeeds
 * }
 */

void drtm_serialise_tcb_hashes_table(char *dst, size_t *size_out)
{
	const struct plat_drtm_tcb_hash *init_hashes;
	size_t num_init_hashes;
	size_t num_hashes_total = 0;
	uintptr_t table_cur = (uintptr_t)dst;

	/* Enumerate all available TCB hashes. */
	plat_enumerate_drtm_tcb_hashes(&init_hashes, &num_init_hashes);
	num_hashes_total += num_init_hashes;

	if (num_hashes_total == 0) {
		goto serialise_tcb_hashes_table_done;
	}

	/* Serialise DRTM TCB_HASHES_TABLE header. */
	struct_drtm_tcb_hash_table_hdr hdr;
	hdr.version = 1;
	hdr.num_hashes = 0;
	hdr.num_hashes += num_init_hashes;
	hdr.hashing_alg = DRTM_TPM_HASH_ALG;

	if (dst) {
		memcpy((char *)table_cur, &hdr, sizeof(hdr));
	}
	table_cur += sizeof(hdr);

	/* Serialise platform DRTM TCB hashes. */
	for (const struct plat_drtm_tcb_hash *plat_h = init_hashes;
		 plat_h < init_hashes + num_init_hashes;
		 plat_h++) {
		struct_drtm_tcb_hash drtm_h;

		drtm_h.hash_id = plat_h->hash_id.uint32;
		/* This assertion follows from the init-time check. */
		assert(plat_h->hash_bytes == sizeof(drtm_h.hash_val));
		/* This assertion follows from the one above and the compile-time one.*/
		assert(plat_h->hash_bytes <= sizeof(plat_h->hash_val));
		memcpy(&drtm_h.hash_val, plat_h->hash_val, plat_h->hash_bytes);

		if (dst) {
			memcpy((char *)table_cur, &drtm_h, sizeof(drtm_h));
		}
		table_cur += sizeof(drtm_h);
	}

serialise_tcb_hashes_table_done:
	/* Return the number of bytes serialised. */
	if (size_out) {
		*size_out = table_cur - (uintptr_t)dst;
	}
}
