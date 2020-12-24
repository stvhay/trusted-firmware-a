/*
 * Copyright (c) 2015-2016, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <string.h>

#include <platform_def.h>

#include <common/bl_common.h>
#include <common/debug.h>
#include <drivers/io/io_driver.h>
#include <drivers/io/io_encrypted.h>
#include <drivers/io/io_fip.h>
#include <drivers/io/io_memmap.h>
#include <drivers/io/io_semihosting.h>
#include <drivers/io/io_storage.h>
#include <lib/semihosting.h>
#include <tools_share/firmware_image_package.h>

/* Semihosting filenames */
#define BL2_IMAGE_NAME			"bl2.bin"
#define BL31_IMAGE_NAME			"bl31.bin"
#define BL32_IMAGE_NAME			"bl32.bin"
#define BL32_EXTRA1_IMAGE_NAME		"bl32_extra1.bin"
#define BL32_EXTRA2_IMAGE_NAME		"bl32_extra2.bin"
#define BL33_IMAGE_NAME			"bl33.bin"

#if TRUSTED_BOARD_BOOT
#define TRUSTED_BOOT_FW_CERT_NAME	"tb_fw.crt"
#define TRUSTED_KEY_CERT_NAME		"trusted_key.crt"
#define SOC_FW_KEY_CERT_NAME		"soc_fw_key.crt"
#define TOS_FW_KEY_CERT_NAME		"tos_fw_key.crt"
#define NT_FW_KEY_CERT_NAME		"nt_fw_key.crt"
#define SOC_FW_CONTENT_CERT_NAME	"soc_fw_content.crt"
#define TOS_FW_CONTENT_CERT_NAME	"tos_fw_content.crt"
#define NT_FW_CONTENT_CERT_NAME		"nt_fw_content.crt"
#endif /* TRUSTED_BOARD_BOOT */

/* Number of images per-set */
#define FWU_N 1
/* Number of sets */
#define FWU_K 2
struct fw_image_metadata_entry {
	uuid_t image_guid;

	/* Address of image A and image B */
	uint64_t image_start[FWU_K];

	uint64_t maximum_image_size;
};

struct fwu_metadata {

	uint32_t header_crc_32;
	uint32_t metadata_version;

	uint32_t active_index;
	uint32_t update_index;

	struct fw_image_metadata_entry image[FWU_N];
};
#define FWU_METADATA_SIZE sizeof(struct fwu_metadata)

void print_metadata(struct fwu_metadata *metadata)
{
	INFO("---metadata--------------------\n");
	INFO("header_crc_32 %x\n", metadata->header_crc_32);
	INFO("metadata_version %x\n", metadata->metadata_version );
	INFO("active_index %x\n", metadata->active_index );
	INFO("update_index %x\n", metadata->update_index );
	INFO("image[0].image_guid %p\n", &metadata->image[0].image_guid);
	INFO("image[0].image_start[0] %llx\n", metadata->image[0].image_start[0]);
	INFO("image[0].image_start[1] %llx\n", metadata->image[0].image_start[1]);
	INFO("image[0].maximum_image_size %llx\n", metadata->image[0].maximum_image_size);
	INFO("-------------------------------\n");
}

/*
uint32_t compute_crc32(uint32_t *buf, uint32_t size)
{
	uint32_t hw_crc32_available;
	static const uint32_t crc_implemented_mask = 0x1<<16;
	uint32_t crc32 = 0;
	uint32_t num_entries = size/sizeof(*buf);

	asm volatile("mrs %0, id_aa64isar0_el1\n":"=r"(hw_crc32_available));

	hw_crc32_available = !!(hw_crc32_available & crc_implemented_mask);
	if(!hw_crc32_available)	{
		ERROR("FWU: crc32 instructions not present -- the prototype code assumes the crc32 are implemented in HW\n");
		panic();
	}

	for(uint32_t *end_buf = buf+num_entries; buf<end_buf; buf++)
	{
		INFO("CRC  %x\n", *buf);
		asm volatile("crc32cw %w0, %w1, %w2": "=r"(crc32): "r"(crc32), "r"(buf));
	}

	return crc32;	
}

uint32_t compute_metadata_crc(struct fwu_metadata *metadata)
{
	uint32_t metadata_crc = compute_crc32(&metadata->metadata_version,
		FWU_METADATA_SIZE - offsetof(struct fwu_metadata, metadata_version));

	INFO("FWU: metadata crc32 %x\n", metadata_crc);

	return metadata_crc;
}
*/

bool is_metadata_intact(struct fwu_metadata *metadata)
{

	/*
	 * XXX: crc32 computation not implemented yet in prototype.
	 * For now metadata corruption is grossly reported when header_crc_32 is zero.
	 */
	return metadata->header_crc_32;
	//return metadata->header_crc_32 == compute_metadata_crc(metadata);
}

/* IO devices */
static const io_dev_connector_t *fip_dev_con;
static uintptr_t fip_dev_handle;
static const io_dev_connector_t *memmap_dev_con;
static uintptr_t memmap_dev_handle;
static const io_dev_connector_t *sh_dev_con;
static uintptr_t sh_dev_handle;
#ifndef DECRYPTION_SUPPORT_none
static const io_dev_connector_t *enc_dev_con;
static uintptr_t enc_dev_handle;
#endif

static const io_block_spec_t fip_block_spec = {
	.offset = PLAT_QEMU_FIP_BASE,
	.length = PLAT_QEMU_FIP_MAX_SIZE
};

static const io_block_spec_t fip_block_spec_b __unused = {
	.offset = PLAT_QEMU_FIP_BASE_B,
	.length = PLAT_QEMU_FIP_MAX_SIZE
};

static const io_uuid_spec_t bl2_uuid_spec = {
	.uuid = UUID_TRUSTED_BOOT_FIRMWARE_BL2,
};

static const io_uuid_spec_t bl31_uuid_spec = {
	.uuid = UUID_EL3_RUNTIME_FIRMWARE_BL31,
};

static const io_uuid_spec_t bl32_uuid_spec = {
	.uuid = UUID_SECURE_PAYLOAD_BL32,
};

static const io_uuid_spec_t bl32_extra1_uuid_spec = {
	.uuid = UUID_SECURE_PAYLOAD_BL32_EXTRA1,
};

static const io_uuid_spec_t bl32_extra2_uuid_spec = {
	.uuid = UUID_SECURE_PAYLOAD_BL32_EXTRA2,
};

static const io_uuid_spec_t bl33_uuid_spec = {
	.uuid = UUID_NON_TRUSTED_FIRMWARE_BL33,
};

#if TRUSTED_BOARD_BOOT
static const io_uuid_spec_t tb_fw_cert_uuid_spec = {
	.uuid = UUID_TRUSTED_BOOT_FW_CERT,
};

static const io_uuid_spec_t trusted_key_cert_uuid_spec = {
	.uuid = UUID_TRUSTED_KEY_CERT,
};

static const io_uuid_spec_t soc_fw_key_cert_uuid_spec = {
	.uuid = UUID_SOC_FW_KEY_CERT,
};

static const io_uuid_spec_t tos_fw_key_cert_uuid_spec = {
	.uuid = UUID_TRUSTED_OS_FW_KEY_CERT,
};

static const io_uuid_spec_t nt_fw_key_cert_uuid_spec = {
	.uuid = UUID_NON_TRUSTED_FW_KEY_CERT,
};

static const io_uuid_spec_t soc_fw_cert_uuid_spec = {
	.uuid = UUID_SOC_FW_CONTENT_CERT,
};

static const io_uuid_spec_t tos_fw_cert_uuid_spec = {
	.uuid = UUID_TRUSTED_OS_FW_CONTENT_CERT,
};

static const io_uuid_spec_t nt_fw_cert_uuid_spec = {
	.uuid = UUID_NON_TRUSTED_FW_CONTENT_CERT,
};
#endif /* TRUSTED_BOARD_BOOT */

static const io_file_spec_t sh_file_spec[] = {
	[BL2_IMAGE_ID] = {
		.path = BL2_IMAGE_NAME,
		.mode = FOPEN_MODE_RB
	},
	[BL31_IMAGE_ID] = {
		.path = BL31_IMAGE_NAME,
		.mode = FOPEN_MODE_RB
	},
	[BL32_IMAGE_ID] = {
		.path = BL32_IMAGE_NAME,
		.mode = FOPEN_MODE_RB
	},
	[BL32_EXTRA1_IMAGE_ID] = {
		.path = BL32_EXTRA1_IMAGE_NAME,
		.mode = FOPEN_MODE_RB
	},
	[BL32_EXTRA2_IMAGE_ID] = {
		.path = BL32_EXTRA2_IMAGE_NAME,
		.mode = FOPEN_MODE_RB
	},
	[BL33_IMAGE_ID] = {
		.path = BL33_IMAGE_NAME,
		.mode = FOPEN_MODE_RB
	},
#if TRUSTED_BOARD_BOOT
	[TRUSTED_BOOT_FW_CERT_ID] = {
		.path = TRUSTED_BOOT_FW_CERT_NAME,
		.mode = FOPEN_MODE_RB
	},
	[TRUSTED_KEY_CERT_ID] = {
		.path = TRUSTED_KEY_CERT_NAME,
		.mode = FOPEN_MODE_RB
	},
	[SOC_FW_KEY_CERT_ID] = {
		.path = SOC_FW_KEY_CERT_NAME,
		.mode = FOPEN_MODE_RB
	},
	[TRUSTED_OS_FW_KEY_CERT_ID] = {
		.path = TOS_FW_KEY_CERT_NAME,
		.mode = FOPEN_MODE_RB
	},
	[NON_TRUSTED_FW_KEY_CERT_ID] = {
		.path = NT_FW_KEY_CERT_NAME,
		.mode = FOPEN_MODE_RB
	},
	[SOC_FW_CONTENT_CERT_ID] = {
		.path = SOC_FW_CONTENT_CERT_NAME,
		.mode = FOPEN_MODE_RB
	},
	[TRUSTED_OS_FW_CONTENT_CERT_ID] = {
		.path = TOS_FW_CONTENT_CERT_NAME,
		.mode = FOPEN_MODE_RB
	},
	[NON_TRUSTED_FW_CONTENT_CERT_ID] = {
		.path = NT_FW_CONTENT_CERT_NAME,
		.mode = FOPEN_MODE_RB
	},
#endif /* TRUSTED_BOARD_BOOT */
};

static int open_fip(const uintptr_t spec);
static int open_memmap(const uintptr_t spec);
#ifndef DECRYPTION_SUPPORT_none
static int open_enc_fip(const uintptr_t spec);
#endif

struct plat_io_policy {
	uintptr_t *dev_handle;
	uintptr_t image_spec;
	int (*check)(const uintptr_t spec);
};

/* By default, ARM platforms load images from the FIP */
static const struct plat_io_policy policies[] = {
	[FIP_IMAGE_ID] = {
		&memmap_dev_handle,
		(uintptr_t)&fip_block_spec,
		open_memmap
	},
	[FIP_IMAGE_ID_B] = {
		&memmap_dev_handle,
		(uintptr_t)&fip_block_spec_b,
		open_memmap
	},
	[ENC_IMAGE_ID] = {
		&fip_dev_handle,
		(uintptr_t)NULL,
		open_fip
	},
	[BL2_IMAGE_ID] = {
		&fip_dev_handle,
		(uintptr_t)&bl2_uuid_spec,
		open_fip
	},
#if ENCRYPT_BL31 && !defined(DECRYPTION_SUPPORT_none)
	[BL31_IMAGE_ID] = {
		&enc_dev_handle,
		(uintptr_t)&bl31_uuid_spec,
		open_enc_fip
	},
#else
	[BL31_IMAGE_ID] = {
		&fip_dev_handle,
		(uintptr_t)&bl31_uuid_spec,
		open_fip
	},
#endif
#if ENCRYPT_BL32 && !defined(DECRYPTION_SUPPORT_none)
	[BL32_IMAGE_ID] = {
		&enc_dev_handle,
		(uintptr_t)&bl32_uuid_spec,
		open_enc_fip
	},
	[BL32_EXTRA1_IMAGE_ID] = {
		&enc_dev_handle,
		(uintptr_t)&bl32_extra1_uuid_spec,
		open_enc_fip
	},
	[BL32_EXTRA2_IMAGE_ID] = {
		&enc_dev_handle,
		(uintptr_t)&bl32_extra2_uuid_spec,
		open_enc_fip
	},
#else
	[BL32_IMAGE_ID] = {
		&fip_dev_handle,
		(uintptr_t)&bl32_uuid_spec,
		open_fip
	},
	[BL32_EXTRA1_IMAGE_ID] = {
		&fip_dev_handle,
		(uintptr_t)&bl32_extra1_uuid_spec,
		open_fip
	},
	[BL32_EXTRA2_IMAGE_ID] = {
		&fip_dev_handle,
		(uintptr_t)&bl32_extra2_uuid_spec,
		open_fip
	},
#endif
	[BL33_IMAGE_ID] = {
		&fip_dev_handle,
		(uintptr_t)&bl33_uuid_spec,
		open_fip
	},
#if TRUSTED_BOARD_BOOT
	[TRUSTED_BOOT_FW_CERT_ID] = {
		&fip_dev_handle,
		(uintptr_t)&tb_fw_cert_uuid_spec,
		open_fip
	},
	[TRUSTED_KEY_CERT_ID] = {
		&fip_dev_handle,
		(uintptr_t)&trusted_key_cert_uuid_spec,
		open_fip
	},
	[SOC_FW_KEY_CERT_ID] = {
		&fip_dev_handle,
		(uintptr_t)&soc_fw_key_cert_uuid_spec,
		open_fip
	},
	[TRUSTED_OS_FW_KEY_CERT_ID] = {
		&fip_dev_handle,
		(uintptr_t)&tos_fw_key_cert_uuid_spec,
		open_fip
	},
	[NON_TRUSTED_FW_KEY_CERT_ID] = {
		&fip_dev_handle,
		(uintptr_t)&nt_fw_key_cert_uuid_spec,
		open_fip
	},
	[SOC_FW_CONTENT_CERT_ID] = {
		&fip_dev_handle,
		(uintptr_t)&soc_fw_cert_uuid_spec,
		open_fip
	},
	[TRUSTED_OS_FW_CONTENT_CERT_ID] = {
		&fip_dev_handle,
		(uintptr_t)&tos_fw_cert_uuid_spec,
		open_fip
	},
	[NON_TRUSTED_FW_CONTENT_CERT_ID] = {
		&fip_dev_handle,
		(uintptr_t)&nt_fw_cert_uuid_spec,
		open_fip
	},
#endif /* TRUSTED_BOARD_BOOT */
};

static uint32_t get_active_fip_image_id(void)
{

	/* TODO: load the metadata from flash rather than relying on emulated AHB reads. */
	struct fwu_metadata *metadata_main =
		(struct fwu_metadata *) PLAT_QEMU_MAIN_METADATA;
	struct fwu_metadata *metadata_fallback =
		(struct fwu_metadata *) PLAT_QEMU_FALLBACK_METADATA;

	struct fwu_metadata *metadata = NULL;

	uint32_t plat_get_trial(void);
	uint32_t trial_run = plat_get_trial();
 
	uint32_t image_selector;


	if (!is_metadata_intact(metadata_main))
	{
		INFO("FWU: Main metadata corrupted\n");
		print_metadata(metadata_main);

		if (!is_metadata_intact(metadata_fallback))
		{
			ERROR("FWU: main and fallback metadata are corrupted\n");
			print_metadata(metadata_fallback);
		} else {
			metadata = metadata_fallback;
		}
	
	} else {
		metadata = metadata_main;
	}

	if (metadata)
	{
		uint32_t active_index = metadata->active_index;
		uint32_t update_index = metadata->update_index;
		image_selector = trial_run > 0 ? update_index : active_index;
	} else {
		/*
		 * Both main and fallback metadata are corrupted. platform must select
		 * rescue image to boot from.
		 * XXX: in this prototype, for prototype simplicity, the rescue image is index 0.
		*/
		image_selector = 0;
	}

	INFO("FWU: image selector %d, trial_run %d\n", image_selector, trial_run);
	switch (image_selector) {
		case 0:
		 INFO("FWU: boot image A\n");
		 return FIP_IMAGE_ID;

		case 1:
		 INFO("FWU: boot image B\n");
		 return FIP_IMAGE_ID_B;

		default:
		 ERROR("erroneous image index %d\n", image_selector);
		 panic();
	}
}

static int open_fip(const uintptr_t spec)
{
	int result;
	uintptr_t local_image_handle;
	uint32_t fip_img_id = get_active_fip_image_id();
	(void)fip_img_id;

	/* See if a Firmware Image Package is available */
	result = io_dev_init(fip_dev_handle, (uintptr_t)fip_img_id);
	if (result == 0 && spec != (uintptr_t)NULL) {
		result = io_open(fip_dev_handle, spec, &local_image_handle);
		if (result == 0) {
			VERBOSE("Using FIP\n");
			io_close(local_image_handle);
		}
	}
	return result;
}

#ifndef DECRYPTION_SUPPORT_none
static int open_enc_fip(const uintptr_t spec)
{
	int result;
	uintptr_t local_image_handle;

	/* See if an encrypted FIP is available */
	result = io_dev_init(enc_dev_handle, (uintptr_t)ENC_IMAGE_ID);
	if (result == 0) {
		result = io_open(enc_dev_handle, spec, &local_image_handle);
		if (result == 0) {
			VERBOSE("Using encrypted FIP\n");
			io_close(local_image_handle);
		}
	}
	return result;
}
#endif

static int open_memmap(const uintptr_t spec)
{
	int result;
	uintptr_t local_image_handle;

	result = io_dev_init(memmap_dev_handle, (uintptr_t)NULL);
	if (result == 0) {
		result = io_open(memmap_dev_handle, spec, &local_image_handle);
		if (result == 0) {
			VERBOSE("Using Memmap\n");
			io_close(local_image_handle);
		}
	}
	return result;
}

static int open_semihosting(const uintptr_t spec)
{
	int result;
	uintptr_t local_image_handle;

	/* See if the file exists on semi-hosting.*/
	result = io_dev_init(sh_dev_handle, (uintptr_t)NULL);
	if (result == 0) {
		result = io_open(sh_dev_handle, spec, &local_image_handle);
		if (result == 0) {
			VERBOSE("Using Semi-hosting IO\n");
			io_close(local_image_handle);
		}
	}
	return result;
}

void plat_qemu_io_setup(void)
{
	int io_result;

	io_result = register_io_dev_fip(&fip_dev_con);
	assert(io_result == 0);

	io_result = register_io_dev_memmap(&memmap_dev_con);
	assert(io_result == 0);

	/* Open connections to devices and cache the handles */
	io_result = io_dev_open(fip_dev_con, (uintptr_t)NULL,
				&fip_dev_handle);
	assert(io_result == 0);

	io_result = io_dev_open(memmap_dev_con, (uintptr_t)NULL,
				&memmap_dev_handle);
	assert(io_result == 0);

#ifndef DECRYPTION_SUPPORT_none
	io_result = register_io_dev_enc(&enc_dev_con);
	assert(io_result == 0);

	io_result = io_dev_open(enc_dev_con, (uintptr_t)NULL,
				&enc_dev_handle);
	assert(io_result == 0);
#endif

	/* Register the additional IO devices on this platform */
	io_result = register_io_dev_sh(&sh_dev_con);
	assert(io_result == 0);

	/* Open connections to devices and cache the handles */
	io_result = io_dev_open(sh_dev_con, (uintptr_t)NULL, &sh_dev_handle);
	assert(io_result == 0);

	/* Ignore improbable errors in release builds */
	(void)io_result;
}

static int get_alt_image_source(unsigned int image_id, uintptr_t *dev_handle,
				  uintptr_t *image_spec)
{
	int result = open_semihosting((const uintptr_t)&sh_file_spec[image_id]);

	if (result == 0) {
		*dev_handle = sh_dev_handle;
		*image_spec = (uintptr_t)&sh_file_spec[image_id];
	}

	return result;
}

/*
 * Return an IO device handle and specification which can be used to access
 * an image. Use this to enforce platform load policy
 */
int plat_get_image_source(unsigned int image_id, uintptr_t *dev_handle,
			  uintptr_t *image_spec)
{
	int result;
	const struct plat_io_policy *policy;
	printf("image_id %d\n", image_id);
	assert(image_id < ARRAY_SIZE(policies));

	policy = &policies[image_id];
	result = policy->check(policy->image_spec);
	if (result == 0) {
		*image_spec = policy->image_spec;
		*dev_handle = *(policy->dev_handle);
	} else {
		VERBOSE("Trying alternative IO\n");
		result = get_alt_image_source(image_id, dev_handle, image_spec);
	}

	return result;
}
