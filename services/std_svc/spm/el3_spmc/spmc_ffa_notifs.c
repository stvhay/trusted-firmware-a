/*
 * Copyright (C) 2021 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * FF-A Notifications book-keeping and signalling.
 *
 * Authors:
 *       Lucian Paul-Trifu <lucian.paultrifu@gmail.com>
 *
 */
#include <assert.h>
#include <endian.h>

#include <smccc_helpers.h>
#include <drivers/arm/gicv3.h>
#include <lib/el3_runtime/pubsub.h>
#include <lib/cassert.h>
#include <lib/spinlock.h>
#include <plat/common/platform.h>
#include <services/ffa_svc.h>

#define FFA_SCHEDULE_RECEIVER_SGI_ID   13

#define enc_unsigned_nonzero(type, v) __extension__ ({ \
	assert(sizeof(type) > sizeof(v)); \
	(type)(v) + (type)1; \
})
#define dec_unsigned_nonzero(type, v) __extension__ ({ \
	assert(sizeof(type) < sizeof(v)); \
	(type)(v) - (type)1; \
})

enum notif_type {
	NOTIF_TYPE_GLOBAL,
	NOTIF_TYPE_VCPU,
};

enum info_get_state {
	NOTIFS_SET_NEEDS_SET,         /* pend_set empty-edge, or initial state; */
	NOTIFS_SET_NEEDS_INFO_GET,    /* pend_set nonempty-edge; */
	NOTIFS_SET_NEEDS_GET,         /* pend_set non-empty, but INFO_GET done. */
};

/*
 * ffa_notifs_set represents an FF-A partition's shared state for purpose of its
 * notification.  An FF-A partition P1 can share part of its state with another
 * partition P2 through the NOTIFICATION_BIND ABI.  Changes in a partition's
 * shared state trigger a "notification" to be sent to the partition.  P2 can
 * set a bit in the state it shares with P1 through the NOTIFICATION_SET ABI,
 * and this will cause a notification to be delivered to P1.  P1 discovers its
 * set bits through the NOTIFICATION_GET ABI, which allows P1 to read its shared
 * state.  P1 can stop sharing its state with P2 through the NOTIFICATION_UNBIND
 * ABI.
 * If P1's partition manager (PM) is distinct from P2's PM, it prepares the
 * latter for this state sharing through the NOTIFICATION_BITMAP _CREATE and
 * _DESTROY ABIs.  The Secure World PM signals all state changes to the
 * Non-secure World PM through the Schedule Receiver Interrupt, and info about
 * which partitions' states changed is channelled through the
 * NOTIFICATION_INFO_GET ABI.
 * FF-A Notifications are an asynchronous communication mechanism designed for
 * reliability.  Since its primitives are merely abstractions of shared state,
 * the mechanism is not subject to capacity limits in the same way messaging is,
 * but on its own it is not suitable for conveying arbitrary data.
 */
#define FFA_NOTIFS_MAX_NUM_RECEIVER_VCPUS   (2 * PLATFORM_CORE_COUNT)
struct ffa_notifs_set {
	unsigned int receiver_id;     /* Owning partition's FF-A ID. */
	size_t receiver_num_vcpus;

	uint64_t pend_set;
	uint64_t pend_set_of_vcpu[FFA_NOTIFS_MAX_NUM_RECEIVER_VCPUS];

	uint64_t bind_set;
	unsigned int bound_senders[64];
	enum notif_type bound_types[64];

	enum info_get_state info_get_state;
	enum info_get_state
	     info_get_state_of_vcpu[FFA_NOTIFS_MAX_NUM_RECEIVER_VCPUS];

	spinlock_t lock;
};

/*
 * All notifications sets, either allocated or not.  The capacity of the array
 * may be freely adjusted, statically.
 */
static struct ffa_notifs_set notifs_sets[1];

/* Notifications sets allocation state. */
static unsigned long notifs_set_is_allocd[ARRAY_SIZE(notifs_sets)];
static spinlock_t alloc_lock;

static bool must_schedule_receiver[PLATFORM_CORE_COUNT];


static struct ffa_notifs_set *get_notifs_unsyncd(unsigned long tag)
{
	if (!tag) {
		return NULL;
	}

	CASSERT(ARRAY_SIZE(notifs_set_is_allocd) == ARRAY_SIZE(notifs_sets),
	        notifs_alloc_info_mismatch);
	for (size_t i = 0; i < ARRAY_SIZE(notifs_set_is_allocd); i++) {
		if (notifs_set_is_allocd[i] == tag) {
			return notifs_sets + i;
		}
	}

	return NULL;
}

static struct ffa_notifs_set *get_or_alloc_notifs(unsigned long tag)
{
	struct ffa_notifs_set *ret = NULL;

	if (!tag) {
		return NULL;
	}

	spin_lock(&alloc_lock);

	if ((ret = get_notifs_unsyncd(tag)) != NULL) {
		goto alloc_done;
	}

	CASSERT(ARRAY_SIZE(notifs_set_is_allocd) == ARRAY_SIZE(notifs_sets),
	        notifs_alloc_info_mismatch);
	for (size_t i = 0; i < ARRAY_SIZE(notifs_set_is_allocd); i++) {
		if (!notifs_set_is_allocd[i]) {
			notifs_set_is_allocd[i] = tag;
			ret = notifs_sets + i;
			break;
		}
	}

alloc_done:
	spin_unlock(&alloc_lock);

	return ret;
}

#define for_each_notifs_set_unsyncd(var) \
	for (struct ffa_notifs_set *(var) = notifs_sets; \
	     (var) - notifs_sets < ARRAY_SIZE(notifs_sets) && \
	     (var) - notifs_sets < ARRAY_SIZE(notifs_set_is_allocd) && \
	     notifs_set_is_allocd[(var) - notifs_sets]; \
	     (var)++)


static void free_notifs(unsigned long tag)
{
	if (!tag) {
		return;
	}

	spin_lock(&alloc_lock);

	for (size_t i = 0; i < ARRAY_SIZE(notifs_set_is_allocd); i++) {
		if (notifs_set_is_allocd[i] == tag) {
			notifs_set_is_allocd[i] = 0;
			break;
		}
	}

	spin_unlock(&alloc_lock);
}


int spmc_ffa_notifications_init_per_pe(void)
{
	unsigned int this_pe = plat_my_core_pos();

	/*
	 * Configure Schedule Receiver SGI as Group-1 Non-Secure.
	 * This enables the Normal World to configure the interrupt further.
	 */
	gicv3_set_interrupt_type(FFA_SCHEDULE_RECEIVER_SGI_ID,
	                         this_pe, INTR_GROUP1NS);

	return 0;
}


uintptr_t spmc_ffa_features_schedule_receiver_int(void *ns_ctx)
{
	SMC_RET3(ns_ctx, FFA_SUCCESS_SMC32, 0, FFA_SCHEDULE_RECEIVER_SGI_ID);
}

uintptr_t spmc_ffa_notification_bitmap_create(uint32_t w1, uint32_t w2,
                                              void *ns_ctx)
{
	uint16_t recv_id = w1;
	size_t recv_num_vcpus = w2;
	struct ffa_notifs_set *new_vm_notifs;

	if (recv_num_vcpus == 0) {
		SMC_RET3(ns_ctx, FFA_ERROR, 0, FFA_ERROR_INVALID_PARAMETER);
	}
	if (recv_num_vcpus > FFA_NOTIFS_MAX_NUM_RECEIVER_VCPUS) {
		SMC_RET3(ns_ctx, FFA_ERROR, 0, FFA_ERROR_NO_MEMORY);
	}

	new_vm_notifs = get_or_alloc_notifs(enc_unsigned_nonzero(unsigned long,
	                                    recv_id));
	if (!new_vm_notifs) {
		SMC_RET3(ns_ctx, FFA_ERROR, 0, FFA_ERROR_NO_MEMORY);
	}

	spin_lock(&new_vm_notifs->lock);

	/*
	 * Ensure that the VM notifs set is actually newly allocated.  get_or_alloc
	 * _notifs() may have returned old ones.
	 */
	if (new_vm_notifs->receiver_num_vcpus != 0) {
		spin_unlock(&new_vm_notifs->lock);
		SMC_RET3(ns_ctx, FFA_ERROR, 0, FFA_ERROR_DENIED);
	}
	new_vm_notifs->receiver_id = recv_id;
	new_vm_notifs->receiver_num_vcpus = recv_num_vcpus;

	/*
	 * All other VM-notif-state sets must have been zero-ed, either because the
	 * VM notifs set was never allocated, or because it was succcessfully
	 * destroyed.
	 */
	assert(new_vm_notifs->pend_set == 0);
	for (size_t i = 0; i < ARRAY_SIZE(new_vm_notifs->pend_set_of_vcpu); i++) {
		assert(new_vm_notifs->pend_set_of_vcpu[i] == 0);
	}
	for (size_t i = 0; i < ARRAY_SIZE(new_vm_notifs->bound_senders); i++) {
		assert(!new_vm_notifs->bound_senders[i]);
	}

	spin_unlock(&new_vm_notifs->lock);

	SMC_RET1(ns_ctx, FFA_SUCCESS_SMC32);
}

uintptr_t spmc_ffa_notification_bitmap_destroy(uint32_t w1, void *ns_ctx)
{
	uint16_t recv_id = w1;
	struct ffa_notifs_set *vms_notifs;
	int err_ret;

	vms_notifs = get_notifs_unsyncd(enc_unsigned_nonzero(unsigned long, recv_id));
	if (!vms_notifs) {
		SMC_RET3(ns_ctx, FFA_ERROR, 0, FFA_ERROR_INVALID_PARAMETER);
	}

	spin_lock(&vms_notifs->lock);

	/*
	 * Check that the VM notifs set are the expected ones.  It may have actually
	 * been freed and/or reallocated, because get_notifs_unsyncd() is not
	 * synchronised, i.e. it is fast.
	 */
	if (!(vms_notifs->receiver_id == recv_id &&
	      vms_notifs->receiver_num_vcpus > 0)) {
		err_ret = FFA_ERROR_INVALID_PARAMETER;
		goto destroy_error;
	}

	/* Check that there are no pending notifications. */
	if (vms_notifs->pend_set != 0) {
		err_ret = FFA_ERROR_DENIED;
		goto destroy_error;
	}
	for (size_t i = 0; i < ARRAY_SIZE(vms_notifs->pend_set_of_vcpu); i++) {
		if (vms_notifs->pend_set_of_vcpu[i] != 0) {
			err_ret = FFA_ERROR_DENIED;
			goto destroy_error;
		}
	}

	/*
	 * Clear the remaining state.
	 * Note that the bind state is cleared, thus implicitly unbinding
	 * all notifications.
	 */
	vms_notifs->receiver_id = 0;
	vms_notifs->receiver_num_vcpus = 0;
	vms_notifs->bind_set = 0;
	for (size_t i = 0; i < ARRAY_SIZE(vms_notifs->bound_senders); i++) {
		vms_notifs->bound_senders[i] = 0;
	}

	spin_unlock(&vms_notifs->lock);

	free_notifs(enc_unsigned_nonzero(unsigned long, recv_id));

	SMC_RET1(ns_ctx, FFA_SUCCESS_SMC32);

destroy_error:
	spin_unlock(&vms_notifs->lock);

	SMC_RET3(ns_ctx, FFA_ERROR, 0, err_ret);
}

uintptr_t spmc_ffa_notification_bind(uint32_t w1, uint32_t w2,
                                     uint32_t w3, uint32_t w4,
                                     void *ns_ctx)
{
	uint16_t recv_id = w1;
	uint16_t sender_id = w1 >> 16;
	enum notif_type notifs_type = (w2 & 0x1U) == 0
	                            ? NOTIF_TYPE_GLOBAL : NOTIF_TYPE_VCPU;
	const uint64_t notifs_set = ((uint64_t)w4 << 32) | w3;
	struct ffa_notifs_set *vms_notifs;
	int err_ret;

	vms_notifs = get_notifs_unsyncd(enc_unsigned_nonzero(unsigned long, recv_id));
	if (!vms_notifs) {
		SMC_RET3(ns_ctx, FFA_ERROR, 0, FFA_ERROR_INVALID_PARAMETER);
	}

	spin_lock(&vms_notifs->lock);

	/*
	 * Check that the VM notifs set are the expected ones.  It may have actually
	 * been freed and/or reallocated, because get_notifs_unsyncd() is not
	 * synchronised, i.e. it is fast.
	 */
	if (!(vms_notifs->receiver_id == recv_id &&
	      vms_notifs->receiver_num_vcpus > 0)) {
		err_ret = FFA_ERROR_INVALID_PARAMETER;
		goto bind_error;
	}

	/* Check that all notifications being bound are currently unbound. */
	if ((vms_notifs->bind_set & notifs_set) != 0) {
		err_ret = FFA_ERROR_DENIED;
		goto bind_error;
	}

	/* Check that none of the notifications being bound are (still) pending. */
	if ((vms_notifs->pend_set & notifs_set) != 0) {
		err_ret = FFA_ERROR_DENIED;
		goto bind_error;
	}
	for (size_t i = 0; i < ARRAY_SIZE(vms_notifs->pend_set_of_vcpu); i++) {
		if ((vms_notifs->pend_set_of_vcpu[i] & notifs_set) != 0) {
			err_ret = FFA_ERROR_DENIED;
			goto bind_error;
		}
	}

	/* Bind the given notifications to the given sender and type. */
	vms_notifs->bind_set |= notifs_set;
	for (uint64_t n = notifs_set, i = 0; n != 0; n >>= 1, i++) {
		if ((n & 0x1U) != 0) {
			vms_notifs->bound_senders[i] = sender_id;
			vms_notifs->bound_types[i] = notifs_type;
		}
	}

	spin_unlock(&vms_notifs->lock);

	SMC_RET1(ns_ctx, FFA_SUCCESS_SMC32);

bind_error:
	spin_unlock(&vms_notifs->lock);

	SMC_RET3(ns_ctx, FFA_ERROR, 0, err_ret);
}

uintptr_t spmc_ffa_notification_unbind(uint32_t w1, uint32_t w3, uint32_t w4,
                                       void *ns_ctx)
{
	uint16_t recv_id = w1;
	uint16_t sender_id = w1 >> 16;
	const uint64_t notifs_set = ((uint64_t)w4 << 32) | w3;
	struct ffa_notifs_set *vms_notifs;
	int err_ret;

	vms_notifs = get_notifs_unsyncd(enc_unsigned_nonzero(unsigned long, recv_id));
	if (!vms_notifs) {
		SMC_RET3(ns_ctx, FFA_ERROR, 0, FFA_ERROR_INVALID_PARAMETER);
	}

	spin_lock(&vms_notifs->lock);

	/*
	 * Check that the VM notifs set are the expected ones.  It may have actually
	 * been freed and/or reallocated, because get_notifs_unsyncd() is not
	 * synchronised, i.e. it is fast.
	 */
	if (!(vms_notifs->receiver_id == recv_id &&
	      vms_notifs->receiver_num_vcpus > 0)) {
		err_ret = FFA_ERROR_INVALID_PARAMETER;
		goto unbind_error;
	}

	/* Validate the Sender ID. */
	for (uint64_t n = notifs_set, i = 0; n != 0; n >>= 1, i++) {
		if ((n & 0x1U) != 0 && sender_id != vms_notifs->bound_senders[i]) {
			err_ret = FFA_ERROR_DENIED;
			goto unbind_error;
		}
	}

	/*
	 * Unbind the given notifications immediately, regardless whether any are
	 * currently pending.  However, preserve the bound Sender IDs so that any
	 * pending notifications can still be retrieved correctly.
	 */
	vms_notifs->bind_set &= ~notifs_set;

	spin_unlock(&vms_notifs->lock);

	SMC_RET1(ns_ctx, FFA_SUCCESS_SMC32);

unbind_error:
	spin_unlock(&vms_notifs->lock);

	SMC_RET3(ns_ctx, FFA_ERROR, 0, err_ret);
}

static void *maybe_pend_schedule_receiver_int(const void *);

uintptr_t spmc_ffa_notification_set(uint32_t w1, uint32_t w2,
                                    uint32_t w3, uint32_t w4,
                                    void *s_ctx)
{
	uint16_t recv_id = w1;
	uint16_t sender_id = w1 >> 16;
	enum notif_type notifs_type = (w2 & 0x1U) == 0
	                            ? NOTIF_TYPE_GLOBAL : NOTIF_TYPE_VCPU;
	uint16_t recv_vcpu_id = w2 >> 16;
	bool delay_schedule_receiver = w2 & 0x2U;
	const uint64_t notifs_set = ((uint64_t)w4 << 32) | w3;
	struct ffa_notifs_set *vms_notifs;
	bool notifs_are_global;
	bool pend_state_nonempty_edge;
	int err_ret;

	vms_notifs = get_notifs_unsyncd(enc_unsigned_nonzero(unsigned long, recv_id));
	if (!vms_notifs) {
		SMC_RET3(s_ctx, FFA_ERROR, 0, FFA_ERROR_INVALID_PARAMETER);
	}

	spin_lock(&vms_notifs->lock);

	/*
	 * Check that the VM notifs set are the expected ones.  It may have actually
	 * been freed and/or reallocated, because get_notifs_unsyncd() is not
	 * synchronised, i.e. it is fast.
	 */
	if (!(vms_notifs->receiver_id == recv_id &&
	      vms_notifs->receiver_num_vcpus > 0)) {
		err_ret = FFA_ERROR_INVALID_PARAMETER;
		goto notif_set_error;
	}

	/* Validate the given vCPU ID. */
	notifs_are_global = (notifs_type == NOTIF_TYPE_GLOBAL);
	if ((notifs_are_global && recv_vcpu_id != 0) ||
	    (!notifs_are_global && recv_vcpu_id >= vms_notifs->receiver_num_vcpus)) {
		err_ret = FFA_ERROR_INVALID_PARAMETER;
		goto notif_set_error;
	}

	/*
	 * Check that the sender is bound to the given notifications and that the
	 * notification type matches.
	 */
	if ((vms_notifs->bind_set & notifs_set) != notifs_set) {
		err_ret = FFA_ERROR_DENIED;
		goto notif_set_error;
	}
	for (uint64_t n = notifs_set, i = 0; n != 0; n >>= 1, i++) {
		if ((n & 0x1U) == 0) {
			continue;
		}
		if (sender_id != vms_notifs->bound_senders[i]) {
			err_ret = FFA_ERROR_DENIED;
			goto notif_set_error;
		}
		if (notifs_type != vms_notifs->bound_types[i]) {
			err_ret = FFA_ERROR_INVALID_PARAMETER;
			goto notif_set_error;
		}
	}

	/* Set the notifications. */
	if (notifs_are_global) {
		pend_state_nonempty_edge = vms_notifs->pend_set == 0 && notifs_set != 0;
		vms_notifs->pend_set |= notifs_set;

		if (pend_state_nonempty_edge) {
			assert(vms_notifs->info_get_state == NOTIFS_SET_NEEDS_SET);
			vms_notifs->info_get_state = NOTIFS_SET_NEEDS_INFO_GET;
		}
	} else {
		uint64_t *pend_set = vms_notifs->pend_set_of_vcpu + recv_vcpu_id;

		pend_state_nonempty_edge = *pend_set == 0 && notifs_set != 0;
		*pend_set |= notifs_set;

		if (pend_state_nonempty_edge) {
			enum info_get_state *info_get_state =
			               vms_notifs->info_get_state_of_vcpu + recv_vcpu_id;

			assert(*info_get_state == NOTIFS_SET_NEEDS_SET);
			*info_get_state = NOTIFS_SET_NEEDS_INFO_GET;
		}
	}

	spin_unlock(&vms_notifs->lock);

	/* Notify the receiver's scheduler if a pending set becomes non-empty. */
	if (pend_state_nonempty_edge) {
		unsigned int this_pe = plat_my_core_pos();

		must_schedule_receiver[this_pe] = true;
		if (!delay_schedule_receiver) {
			maybe_pend_schedule_receiver_int(NULL);
		}
	}

	SMC_RET1(s_ctx, FFA_SUCCESS_SMC32);

notif_set_error:
	spin_unlock(&vms_notifs->lock);

	SMC_RET3(s_ctx, FFA_ERROR, 0, err_ret);
}

uintptr_t spmc_ffa_notification_get(uint32_t w1, uint32_t w2, void *ns_ctx)
{
	uint16_t recv_id = w1;
	uint16_t recv_vcpu_id = w1 >> 16;
	bool get_sp_sent_notifs = w2 >> 0 & 0x1;
	bool get_vm_sent_notifs = w2 >> 1 & 0x1;
	bool get_spm_sent_notifs = w2 >> 2 & 0x1;
	bool get_hyp_sent_notifs = w2 >> 3 & 0x1;
	struct ffa_notifs_set *vms_notifs;
	uint64_t sp_sent_notifs = 0;
	int err_ret;

	if (get_vm_sent_notifs) {
		WARN("SPMC: FFA_NOTIFICATION_GET unimplemented 'get_vm_sent_notifs'\n");
		SMC_RET3(ns_ctx, FFA_ERROR, 0, FFA_ERROR_INVALID_PARAMETER);
	}
	if (get_hyp_sent_notifs) {
		WARN("SPMC: FFA_NOTIFICATION_GET unimplemented 'get_hyp_sent_notifs'\n");
		SMC_RET3(ns_ctx, FFA_ERROR, 0, FFA_ERROR_INVALID_PARAMETER);
	}

	if (get_sp_sent_notifs) {
		vms_notifs = get_notifs_unsyncd(enc_unsigned_nonzero(unsigned long,
		                               recv_id));
		if (!vms_notifs) {
			SMC_RET3(ns_ctx, FFA_ERROR, 0, FFA_ERROR_INVALID_PARAMETER);
		}

		spin_lock(&vms_notifs->lock);

		/*
		 * Check that the VM notifs set are the expected ones.  It may have
		 * actually been freed and/or reallocated, because get_notifs_unsyncd()
		 * is not synchronised, i.e. it is fast.
		 */
		if (!(vms_notifs->receiver_id == recv_id &&
		      vms_notifs->receiver_num_vcpus > 0)) {
			err_ret = FFA_ERROR_INVALID_PARAMETER;
			goto notif_get_sp_sent_error;
		}

		/* Validate the given vCPU ID. */
		if (recv_vcpu_id >= vms_notifs->receiver_num_vcpus) {
			err_ret = FFA_ERROR_INVALID_PARAMETER;
			goto notif_get_sp_sent_error;
		}

		/* Get the global pending notifications. */
		sp_sent_notifs |= vms_notifs->pend_set;
		vms_notifs->pend_set = 0;
		vms_notifs->info_get_state = NOTIFS_SET_NEEDS_SET;

		/* Get the per-vCPU pending notifications. */
		sp_sent_notifs |= vms_notifs->pend_set_of_vcpu[recv_vcpu_id];
		vms_notifs->pend_set_of_vcpu[recv_vcpu_id] = 0;
		vms_notifs->info_get_state_of_vcpu[recv_vcpu_id] = NOTIFS_SET_NEEDS_SET;

		spin_unlock(&vms_notifs->lock);

		SMC_SET_GP(ns_ctx, CTX_GPREG_X2, (uint32_t)sp_sent_notifs);
		SMC_SET_GP(ns_ctx, CTX_GPREG_X3, (uint32_t)(sp_sent_notifs >> 32));
	}

	if (get_spm_sent_notifs) {
		SMC_SET_GP(ns_ctx, CTX_GPREG_X6, 0);
	}

	SMC_RET1(ns_ctx, FFA_SUCCESS_SMC32);

notif_get_sp_sent_error:
	spin_unlock(&vms_notifs->lock);
	SMC_RET3(ns_ctx, FFA_ERROR, 0, err_ret);
}

uintptr_t spmc_ffa_notification_info_get(void *ns_ctx)
{
	/*
	 * The width of `r2' is the width assumed for the return-values regs.
	 * Changing it switches between the SMC32 and SMC64 conventions, compile-
	 * time.
	 */
	//uint32_t r2;  /* Use the SMC32 variant. */
	uint64_t r2;  /* Use the SMC64 variant. */
	/*
	 * The `ids' union holds the IDs that are going to be packed into the return
	 * -values regs.  `ids.el' is the storage for each ID element, whereas
	 * `ids.ret_reg' is an accessor to the value of each return-value register.
	 * The size of the `ids.el' array is:
	 *       num_return_regs * (sizeof_a_return_reg / sizeof_id_field)
	 */
	union {
		uint16_t el[5 * sizeof(r2) / sizeof(uint16_t)];
		const typeof(r2) ret_reg[5];
	} ids = { .el = {0} };
	const uint16_t *const ids_end = ids.el + ARRAY_SIZE(ids.el);
	uint16_t *id_cur = ids.el;
	unsigned int lists_lengths[ARRAY_SIZE(ids.el)] = {0};
	unsigned int *list_len_cur = lists_lengths;
#   define ID_LIST_LENGTH_FIELD_SIZE   2
#   define MAX_ID_LIST_LEN   (1U << ID_LIST_LENGTH_FIELD_SIZE)
	bool out_of_space;

	for_each_notifs_set_unsyncd(notifs) {
		spin_lock(&notifs->lock);

		/*
		 * Check that the VM notifs set are valid.  They may have actually been
		 * freed, because for_each_notifs_set_unsyncd() is not synchronised.
		 */
		if (notifs->receiver_num_vcpus == 0) {
			goto loop_continue;
		}

		/* Encode global notification info. */
		if (notifs->info_get_state == NOTIFS_SET_NEEDS_INFO_GET) {
			assert(notifs->pend_set != 0);

			if ((out_of_space = ids_end - id_cur < 1)) {
				goto loop_exit;
			}
			// Begin ID list of length == 1
			assert(*list_len_cur == 0);
			(*list_len_cur)++, *id_cur++ = notifs->receiver_id;
			// End ID list of length == 1
			list_len_cur++;

			notifs->info_get_state = NOTIFS_SET_NEEDS_GET;
		}

		/* Find vcpu notifications needing INFO_GET, move on if there are none. */
		size_t vcpu_id;
		for (vcpu_id = 0;
		     vcpu_id < notifs->receiver_num_vcpus &&
		     notifs->info_get_state_of_vcpu[vcpu_id]
		      != NOTIFS_SET_NEEDS_INFO_GET;
		     vcpu_id++)
		     ;
		if (vcpu_id >= notifs->receiver_num_vcpus) {
			goto loop_continue;
		}
		assert(notifs->pend_set_of_vcpu[vcpu_id] != 0);

		/* Encode per-vcpu notification info. */
		if ((out_of_space = ids_end - id_cur < 2)) {
			goto loop_exit;
		}
		// Begin ID list of length >= 2
		assert(*list_len_cur == 0);
		(*list_len_cur)++, *id_cur++ = notifs->receiver_id;

		for (; vcpu_id < notifs->receiver_num_vcpus; vcpu_id++) {
			if (notifs->info_get_state_of_vcpu[vcpu_id]
			     != NOTIFS_SET_NEEDS_INFO_GET) {
				continue;
			}
			assert(notifs->pend_set_of_vcpu[vcpu_id] != 0);

			if (*list_len_cur == MAX_ID_LIST_LEN) {
				if ((out_of_space = ids_end - id_cur < 2)) {
					break;
				}
				// End ID list of length == MAX_ID_LIST_LEN
				list_len_cur++;
				// Begin ID list of length >= 2
				assert(*list_len_cur == 0);
				(*list_len_cur)++, *id_cur++ = notifs->receiver_id;
			}

			if ((out_of_space = ids_end - id_cur < 1)) {
				break;
			}
			(*list_len_cur)++, *id_cur++ = vcpu_id;

			notifs->info_get_state_of_vcpu[vcpu_id] = NOTIFS_SET_NEEDS_GET;
		}
		// End ID list of length <= MAX_ID_LIST_LEN
		assert(*list_len_cur <= MAX_ID_LIST_LEN);
		list_len_cur++;

		if (out_of_space) {
			goto loop_exit;
		}

	loop_continue:
		spin_unlock(&notifs->lock);
		continue;

	loop_exit:
		spin_unlock(&notifs->lock);
		break;
	}

	/* Return NO_DATA if there are no IDs in the list compiled previously. */
	if (id_cur - ids.el == 0) {
		SMC_RET3(ns_ctx, FFA_ERROR, 0, FFA_ERROR_NO_DATA);
	}

	r2 = 0U;
	r2 |= (unsigned int)out_of_space << 0;
	r2 |= ((unsigned int)(list_len_cur - lists_lengths) & 0x1f) << 7;
	for (size_t i = 0; i < list_len_cur - lists_lengths; i++) {
		r2 |= ((lists_lengths[i] - 1) & ((1 << ID_LIST_LENGTH_FIELD_SIZE) - 1))
		      << (ID_LIST_LENGTH_FIELD_SIZE * i + 12);
	}

	SMC_RET8(ns_ctx, sizeof(r2) == sizeof(uint32_t) ?
			 FFA_SUCCESS_SMC32 : FFA_SUCCESS_SMC64,
			 0,
			 r2,
#if _BYTE_ORDER == _BIG_ENDIAN
#error The union access below will place ID-list elements in the wrong order; \
       half-word order must be swapped.
#endif
			 ids.ret_reg[0],
			 ids.ret_reg[1],
			 ids.ret_reg[2],
			 ids.ret_reg[3],
			 ids.ret_reg[4]);
}


static void *maybe_pend_schedule_receiver_int(const void *null)
{
	unsigned int this_pe = plat_my_core_pos();

	/* Set pending the Schedule Receiver SGI, if it is due. */
	if (must_schedule_receiver[this_pe]) {
		gicv3_set_interrupt_pending(FFA_SCHEDULE_RECEIVER_SGI_ID, this_pe);
		must_schedule_receiver[this_pe] = false;
	}

	return NULL;
}
SUBSCRIBE_TO_EVENT(cm_entering_normal_world, maybe_pend_schedule_receiver_int);
