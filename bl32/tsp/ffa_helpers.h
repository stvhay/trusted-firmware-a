/*
 * Copyright (c) 2018-2021, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#if SPMC_AT_EL3

#ifndef FFA_HELPERS_H
#define FFA_HELPERS_H

#include <stdint.h>
#include <services/ffa_svc.h>
#include "tsp_private.h"


typedef unsigned short ffa_id_t;
typedef uint64_t ffa_memory_handle_t;
/** Flags to indicate properties of receivers during memory region retrieval. */
typedef uint8_t ffa_memory_receiver_flags_t;

struct ffa_uuid {
	const uint32_t uuid[4];
};

static inline uint32_t ffa_func_id(tsp_args_t val) {
	return (uint32_t) val._regs[0];
}

static inline int32_t ffa_error_code(tsp_args_t val) {
	return (uint32_t) val._regs[2];
}

enum ffa_data_access {
	FFA_DATA_ACCESS_NOT_SPECIFIED,
	FFA_DATA_ACCESS_RO,
	FFA_DATA_ACCESS_RW,
	FFA_DATA_ACCESS_RESERVED,
};

enum ffa_instruction_access {
	FFA_INSTRUCTION_ACCESS_NOT_SPECIFIED,
	FFA_INSTRUCTION_ACCESS_NX,
	FFA_INSTRUCTION_ACCESS_X,
	FFA_INSTRUCTION_ACCESS_RESERVED,
};

enum ffa_memory_type {
	FFA_MEMORY_NOT_SPECIFIED_MEM,
	FFA_MEMORY_DEVICE_MEM,
	FFA_MEMORY_NORMAL_MEM,
};

enum ffa_memory_cacheability {
	FFA_MEMORY_CACHE_RESERVED = 0x0,
	FFA_MEMORY_CACHE_NON_CACHEABLE = 0x1,
	FFA_MEMORY_CACHE_RESERVED_1 = 0x2,
	FFA_MEMORY_CACHE_WRITE_BACK = 0x3,
	FFA_MEMORY_DEV_NGNRNE = 0x0,
	FFA_MEMORY_DEV_NGNRE = 0x1,
	FFA_MEMORY_DEV_NGRE = 0x2,
	FFA_MEMORY_DEV_GRE = 0x3,
};

enum ffa_memory_shareability {
	FFA_MEMORY_SHARE_NON_SHAREABLE,
	FFA_MEMORY_SHARE_RESERVED,
	FFA_MEMORY_OUTER_SHAREABLE,
	FFA_MEMORY_INNER_SHAREABLE,
};

typedef uint8_t ffa_memory_access_permissions_t;

/**
 * This corresponds to table "Memory region attributes descriptor" of the FF-A
 * 1.0 specification.
 */
typedef uint8_t ffa_memory_attributes_t;

#define FFA_DATA_ACCESS_OFFSET (0x0U)
#define FFA_DATA_ACCESS_MASK ((0x3U) << FFA_DATA_ACCESS_OFFSET)

#define FFA_INSTRUCTION_ACCESS_OFFSET (0x2U)
#define FFA_INSTRUCTION_ACCESS_MASK ((0x3U) << FFA_INSTRUCTION_ACCESS_OFFSET)

#define FFA_MEMORY_TYPE_OFFSET (0x4U)
#define FFA_MEMORY_TYPE_MASK ((0x3U) << FFA_MEMORY_TYPE_OFFSET)

#define FFA_MEMORY_CACHEABILITY_OFFSET (0x2U)
#define FFA_MEMORY_CACHEABILITY_MASK ((0x3U) << FFA_MEMORY_CACHEABILITY_OFFSET)

#define FFA_MEMORY_SHAREABILITY_OFFSET (0x0U)
#define FFA_MEMORY_SHAREABILITY_MASK ((0x3U) << FFA_MEMORY_SHAREABILITY_OFFSET)

#define ATTR_FUNCTION_SET(name, container_type, offset, mask)                \
	static inline void ffa_set_##name##_attr(container_type *attr,       \
						 const enum ffa_##name perm) \
	{                                                                    \
		*attr = (*attr & ~(mask)) | ((perm << offset) & mask);       \
	}

#define ATTR_FUNCTION_GET(name, container_type, offset, mask)      \
	static inline enum ffa_##name ffa_get_##name##_attr(       \
		container_type attr)                               \
	{                                                          \
		return (enum ffa_##name)((attr & mask) >> offset); \
	}

ATTR_FUNCTION_SET(data_access, ffa_memory_access_permissions_t,
		  FFA_DATA_ACCESS_OFFSET, FFA_DATA_ACCESS_MASK)
ATTR_FUNCTION_GET(data_access, ffa_memory_access_permissions_t,
		  FFA_DATA_ACCESS_OFFSET, FFA_DATA_ACCESS_MASK)

ATTR_FUNCTION_SET(instruction_access, ffa_memory_access_permissions_t,
		  FFA_INSTRUCTION_ACCESS_OFFSET, FFA_INSTRUCTION_ACCESS_MASK)
ATTR_FUNCTION_GET(instruction_access, ffa_memory_access_permissions_t,
		  FFA_INSTRUCTION_ACCESS_OFFSET, FFA_INSTRUCTION_ACCESS_MASK)

ATTR_FUNCTION_SET(memory_type, ffa_memory_attributes_t, FFA_MEMORY_TYPE_OFFSET,
		  FFA_MEMORY_TYPE_MASK)
ATTR_FUNCTION_GET(memory_type, ffa_memory_attributes_t, FFA_MEMORY_TYPE_OFFSET,
		  FFA_MEMORY_TYPE_MASK)

ATTR_FUNCTION_SET(memory_cacheability, ffa_memory_attributes_t,
		  FFA_MEMORY_CACHEABILITY_OFFSET, FFA_MEMORY_CACHEABILITY_MASK)
ATTR_FUNCTION_GET(memory_cacheability, ffa_memory_attributes_t,
		  FFA_MEMORY_CACHEABILITY_OFFSET, FFA_MEMORY_CACHEABILITY_MASK)

ATTR_FUNCTION_SET(memory_shareability, ffa_memory_attributes_t,
		  FFA_MEMORY_SHAREABILITY_OFFSET, FFA_MEMORY_SHAREABILITY_MASK)
ATTR_FUNCTION_GET(memory_shareability, ffa_memory_attributes_t,
		  FFA_MEMORY_SHAREABILITY_OFFSET, FFA_MEMORY_SHAREABILITY_MASK)

#define FFA_MEMORY_HANDLE_ALLOCATOR_MASK \
	((ffa_memory_handle_t)(UINT64_C(1) << 63))
#define FFA_MEMORY_HANDLE_ALLOCATOR_HYPERVISOR \
	((ffa_memory_handle_t)(UINT64_C(1) << 63))
#define FFA_MEMORY_HANDLE_INVALID (~UINT64_C(0))

/**
 * A set of contiguous pages which is part of a memory region. This corresponds
 * to table "Constituent memory region descriptor" of the FFA 1.0 specification.
 */
struct ffa_memory_region_constituent {
	/**
	 * The base IPA of the constituent memory region, aligned to 4 kiB page
	 * size granularity.
	 */
	void *address;
	/** The number of 4 kiB pages in the constituent memory region. */
	uint32_t page_count;
	/** Reserved field, must be 0. */
	uint32_t reserved;
};

/**
 * A set of pages comprising a memory region. This corresponds to table
 * "Composite memory region descriptor" of the FFA 1.0 specification.
 */
struct ffa_composite_memory_region {
	/**
	 * The total number of 4 kiB pages included in this memory region. This
	 * must be equal to the sum of page counts specified in each
	 * `ffa_memory_region_constituent`.
	 */
	uint32_t page_count;
	/**
	 * The number of constituents (`ffa_memory_region_constituent`)
	 * included in this memory region range.
	 */
	uint32_t constituent_count;
	/** Reserved field, must be 0. */
	uint64_t reserved_0;
	/** An array of `constituent_count` memory region constituents. */
	struct ffa_memory_region_constituent constituents[];
};

/**
 * This corresponds to table "Memory access permissions descriptor" of the FFA
 * 1.0 specification.
 */
struct ffa_memory_region_attributes {
	/** The ID of the VM to which the memory is being given or shared. */
	ffa_id_t receiver;
	/**
	 * The permissions with which the memory region should be mapped in the
	 * receiver's page table.
	 */
	ffa_memory_access_permissions_t permissions;
	/**
	 * Flags used during FFA_MEM_RETRIEVE_REQ and FFA_MEM_RETRIEVE_RESP
	 * for memory regions with multiple borrowers.
	 */
	ffa_memory_receiver_flags_t flags;
};

/** Flags to control the behaviour of a memory sharing transaction. */
typedef uint32_t ffa_memory_region_flags_t;

/**
 * Clear memory region contents after unmapping it from the sender and before
 * mapping it for any receiver.
 */
#define FFA_MEMORY_REGION_FLAG_CLEAR 0x1U

/**
 * Whether the hypervisor may time slice the memory sharing or retrieval
 * operation.
 */
#define FFA_MEMORY_REGION_FLAG_TIME_SLICE 0x2U

/**
 * Whether the hypervisor should clear the memory region after the receiver
 * relinquishes it or is aborted.
 */
#define FFA_MEMORY_REGION_FLAG_CLEAR_RELINQUISH 0x4U

#define FFA_MEMORY_REGION_TRANSACTION_TYPE_MASK ((0x3U) << 3)
#define FFA_MEMORY_REGION_TRANSACTION_TYPE_UNSPECIFIED ((0x0U) << 3)
#define FFA_MEMORY_REGION_TRANSACTION_TYPE_SHARE ((0x1U) << 3)
#define FFA_MEMORY_REGION_TRANSACTION_TYPE_LEND ((0x2U) << 3)
#define FFA_MEMORY_REGION_TRANSACTION_TYPE_DONATE ((0x3U) << 3)

/** The maximum number of recipients a memory region may be sent to. */
#define MAX_MEM_SHARE_RECIPIENTS 1U


/* FFA Memory mapping modes. */
#define FFA_MEM_EXEC	0x8
#define FFA_MEM_NO_EXEC	0x4
#define FFA_MEM_RW		0x2
#define FFA_MEM_RO		0x1

/* FFA Memory Management mode flags. */
#define FFA_FLAG_SHARE_MEMORY (1U << 3)
#define FFA_FLAG_LEND_MEMORY (1U << 4)

#define FFA_FLAG_MEMORY_MASK (3U << 3)

#define FFA_MEM_HANDLE_LOW(x) (x & 0xFFFFFFFF)
#define FFA_MEM_HANDLE_HIGH(x) (x >> 32)

/* FF-A Run Macros. */
#define FFA_VCPU_ID_MASK 0xFFFF
#define FFA_RUN_VCPU(id) \
               ((id >> 0) & FFA_VCPU_ID_MASK)
/**
 * This corresponds to table "Endpoint memory access descriptor" of the FFA 1.0
 * specification.
 */
struct ffa_memory_access {
	struct ffa_memory_region_attributes receiver_permissions;
	/**
	 * Offset in bytes from the start of the outer `ffa_memory_region` to
	 * an `ffa_composite_memory_region` struct.
	 */
	uint32_t composite_memory_region_offset;
	uint64_t reserved_0;
};

/**
 * Information about a set of pages which are being shared. This corresponds to
 * table "Lend, donate or share memory transaction descriptor" of the FFA
 * 1.0 specification. Note that it is also used for retrieve requests and
 * responses.
 */
struct ffa_memory_region {
	/**
	 * The ID of the VM which originally sent the memory region, i.e. the
	 * owner.
	 */
	ffa_id_t sender;
	ffa_memory_attributes_t attributes;
	/** Reserved field, must be 0. */
	uint8_t reserved_0;
	/** Flags to control behaviour of the transaction. */
	ffa_memory_region_flags_t flags;
	ffa_memory_handle_t handle;
	/**
	 * An implementation defined value associated with the receiver and the
	 * memory region.
	 */
	uint64_t tag;
	/** Reserved field, must be 0. */
	uint32_t reserved_1;
	/**
	 * The number of `ffa_memory_access` entries included in this
	 * transaction.
	 */
	uint32_t receiver_count;
	/**
	 * An array of `attribute_count` endpoint memory access descriptors.
	 * Each one specifies a memory region offset, an endpoint and the
	 * attributes with which this memory region should be mapped in that
	 * endpoint's page table.
	 */
	struct ffa_memory_access receivers[];
};

/**
 * Descriptor used for FFA_MEM_RELINQUISH requests. This corresponds to table
 * "Descriptor to relinquish a memory region" of the FFA 1.0 specification.
 */
struct ffa_mem_relinquish {
	ffa_memory_handle_t handle;
	ffa_memory_region_flags_t flags;
	uint32_t endpoint_count;
	ffa_id_t endpoints[];
};


static inline uint32_t ffa_mem_relinquish_init(
	struct ffa_mem_relinquish *relinquish_request,
	ffa_memory_handle_t handle, ffa_memory_region_flags_t flags,
	ffa_id_t sender)
{
	relinquish_request->handle = handle;
	relinquish_request->flags = flags;
	relinquish_request->endpoint_count = 1;
	relinquish_request->endpoints[0] = sender;
	return sizeof(struct ffa_mem_relinquish) + sizeof(ffa_id_t);
}

/**
 * Gets the `ffa_composite_memory_region` for the given receiver from an
 * `ffa_memory_region`, or NULL if it is not valid.
 */
static inline struct ffa_composite_memory_region *
ffa_memory_region_get_composite(struct ffa_memory_region *memory_region,
				uint32_t receiver_index)
{
	uint32_t offset = memory_region->receivers[receiver_index]
				  .composite_memory_region_offset;

	if (offset == 0) {
		return NULL;
	}

	return (struct ffa_composite_memory_region *)((uint8_t *)memory_region +
						      offset);
}


/**
 * Initialises the given `ffa_memory_region` to be used for an
 * `FFA_MEM_RETRIEVE_REQ` by the receiver of a memory transaction.
 *
 * Returns the size of the message written.
 */
uint32_t ffa_memory_retrieve_request_init(
	struct ffa_memory_region *memory_region, ffa_memory_handle_t handle,
	ffa_id_t sender, ffa_id_t receiver, uint32_t tag,
	ffa_memory_region_flags_t flags, enum ffa_data_access data_access,
	enum ffa_instruction_access instruction_access,
	enum ffa_memory_type type, enum ffa_memory_cacheability cacheability,
	enum ffa_memory_shareability shareability);

tsp_args_t ffa_mem_frag_rx(uint64_t handle, uint32_t recv_length);
tsp_args_t ffa_mem_retrieve_req(uint32_t descriptor_length,
			            uint32_t fragment_length);
bool ffa_mem_relinquish(void);
bool ffa_rx_release(void);
bool memory_relinquish(struct ffa_mem_relinquish *m, uint64_t handle,
		       ffa_id_t id);
bool ffa_rxtx_map(uintptr_t send, uintptr_t recv, uint32_t pages);


#endif /* FFA_HELPERS_H */
#endif /* SPMC_AT_EL3 */
