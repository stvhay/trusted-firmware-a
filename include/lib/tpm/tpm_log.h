/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 */
#ifndef TPM_LOG_H
#define TPM_LOG_H

#include <stddef.h>

#include <lib/tpm/tpm.h>
#include <export/lib/utils_def_exp.h>

/*
 * Event types
 * Ref. Table 9 Events
 * TCG PC Client Platform Firmware Profile Specification,
 * Family "2.0", Level 00 Revision 1.04, June 3 2019.
 */
#define TPM_LOG_EV_PREBOOT_CERT				U(0x00000000)
#define TPM_LOG_EV_POST_CODE				U(0x00000001)
#define TPM_LOG_EV_UNUSED				U(0x00000002)
#define TPM_LOG_EV_NO_ACTION				U(0x00000003)
#define TPM_LOG_EV_SEPARATOR				U(0x00000004)
#define TPM_LOG_EV_ACTION				U(0x00000005)
#define TPM_LOG_EV_EVENT_TAG				U(0x00000006)
#define TPM_LOG_EV_S_CRTM_CONTENTS			U(0x00000007)
#define TPM_LOG_EV_S_CRTM_VERSION			U(0x00000008)
#define TPM_LOG_EV_CPU_MICROCODE			U(0x00000009)
#define TPM_LOG_EV_PLATFORM_CONFIG_FLAGS		U(0x0000000A)
#define TPM_LOG_EV_TABLE_OF_DEVICES			U(0x0000000B)
#define TPM_LOG_EV_COMPACT_HASH				U(0x0000000C)
#define TPM_LOG_EV_IPL					U(0x0000000D)
#define TPM_LOG_EV_IPL_PARTITION_DATA			U(0x0000000E)
#define TPM_LOG_EV_NONHOST_CODE				U(0x0000000F)
#define TPM_LOG_EV_NONHOST_CONFIG			U(0x00000010)
#define TPM_LOG_EV_NONHOST_INFO				U(0x00000011)
#define TPM_LOG_EV_OMIT_BOOT_DEVICE_EVENTS		U(0x00000012)
#define TPM_LOG_EV_EFI_EVENT_BASE			U(0x80000000)
#define TPM_LOG_EV_EFI_VARIABLE_DRIVER_CONFIG		U(0x80000001)
#define TPM_LOG_EV_EFI_VARIABLE_BOOT			U(0x80000002)
#define TPM_LOG_EV_EFI_BOOT_SERVICES_APPLICATION	U(0x80000003)
#define TPM_LOG_EV_EFI_BOOT_SERVICES_DRIVER		U(0x80000004)
#define TPM_LOG_EV_EFI_RUNTIME_SERVICES_DRIVER		U(0x80000005)
#define TPM_LOG_EV_EFI_GPT_EVENT			U(0x80000006)
#define TPM_LOG_EV_EFI_ACTION				U(0x80000007)
#define TPM_LOG_EV_EFI_PLATFORM_FIRMWARE_BLOB		U(0x80000008)
#define TPM_LOG_EV_EFI_HANDOFF_TABLES			U(0x80000009)
#define TPM_LOG_EV_EFI_HCRTM_EVENT			U(0x80000010)
#define TPM_LOG_EV_EFI_VARIABLE_AUTHORITY		U(0x800000E0)


struct tpm_log_digest {
	enum tpm_hash_alg h_alg;
	size_t buf_bytes;
	char buf[];
};

struct tpm_log_digests {
	size_t count;
	struct tpm_log_digest d[];
};

struct tpm_log_info {
	char *buf;
	size_t buf_bytes;

	/* Running cursor, into the buffer. */
	char *cursor;

	/* */
	char *startup_locality_event_data;
};
/* Opaque / encapsulated type */
typedef struct tpm_log_info tpm_log_info_t;


int tpm_log_init(uint32_t *const tpm_log_buf, size_t tpm_log_buf_bytes,
                 enum tpm_hash_alg alg[], size_t num_algs,
                 tpm_log_info_t *log_info_out);
int tpm_log_add_event(tpm_log_info_t *tpm_log_info,
                      uint32_t event_type, enum tpm_pcr_idx pcr,
                      struct tpm_log_digests *digests,
                      const unsigned char *event_data, size_t event_data_bytes);
void tpm_log_serialise(char *dst, const tpm_log_info_t *tpm_log,
                       size_t *tpm_log_size_out);


#endif /* TPM_LOG_H */
