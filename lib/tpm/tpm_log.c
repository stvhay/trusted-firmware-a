/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <lib/tpm/tpm.h>
#include <lib/tpm/tpm_log.h>

#include "tpm_log_private.h"


/*
 * TODO: The struct serialisation handling in this library is prone to alignment
 * faults.  It happens to work because the TCG-defined structure fields
 * generally maintain natural alignment.  And it avoids undefined C-language
 * behaviour thanks to #pragma pack(1).
 * However, future extensions of this library could introduce structures whose
 * serialisation would break the natural alignment.  For example, serialising
 * vendor-specific info structures.
 * Therefore, it would be an improvement if a more standard way of serialising
 * struct was used, such as preparing structs on the stack first, and then
 * serialising them to the destination via memcpy().
 */

static const id_event_container_t id_event_templ = {
	.container = {
		.pcr_index = TPM_PCR_0,
		.event_type = TPM_LOG_EV_NO_ACTION,
		.digest = {0},
		/*
		 * Must be set at run-time, when hash_alg_count and the number of
		 * vendor bytes is given:
		 *    .event_size = ...;
		 */
	},
	.id_event_misc_data = {
		.signature = TCG_ID_EVENT_SIGNATURE_03,
		.platform_class = PLATFORM_CLASS_CLIENT,
		.spec_version_minor = TCG_SPEC_VERSION_MINOR_TPM2,
		.spec_version_major = TCG_SPEC_VERSION_MAJOR_TPM2,
		.spec_errata = TCG_SPEC_ERRATA_TPM2,
		.uintn_size = (uint8_t)(sizeof(unsigned int) /
					sizeof(uint32_t)),
		/*
		 * Must be set to hash_alg_count
		 *    .number_of_algorithms = hash_alg_count
		 */
	}
};

static const event2_header_t startup_event_container_templ = {
	/*
	 * All EV_NO_ACTION events SHALL set
	 * TCG_PCR_EVENT2.pcrIndex = 0, unless otherwise specified
	 */
	.pcr_index = TPM_PCR_0,

	/*
	 * All EV_NO_ACTION events SHALL set
	 * TCG_PCR_EVENT2.eventType = 03h
	 */
	.event_type = TPM_LOG_EV_NO_ACTION,

	/*
	 * All EV_NO_ACTION events SHALL set
	 * TCG_PCR_EVENT2.digests to all
	 * 0x00's for each allocated Hash algorithm
	 *
	 * Must be set at runtime, when hash_alg_count is known:
	 *    .digests = {
	 *    	.count = hash_alg_count,
	 *    	.digests = {alg_id1, {0}, alg_id2, {0}, ...}
	 *    }
	 */
};
static const startup_locality_event_t startup_event_templ = {
	.startup_event_header = {
		.event_size = sizeof(startup_locality_event_data_t),
	},
	.startup_event_data = {
		.signature = TCG_STARTUP_LOCALITY_SIGNATURE,
		/*
		 * Must be set at run time, when startup_locality is provided:
		 *    .startup_locality = startup_locality
		 */
	}
};


int tpm_log_init(uint32_t *const buf, size_t buf_bytes,
                 enum tpm_hash_alg alg[], size_t num_algs,
                 struct tpm_log_info *log_out)
{
	const char *const buf_end = (char *)buf + buf_bytes;
	char *cur , *cur_next;
	char *id_event;

	for (int i = 0; i < num_algs; i++) {
		if (!tpm_alg_is_valid(alg[i])) {
			return -EINVAL;
		}
	}

	cur = (char *)buf;
	cur_next = cur + sizeof(id_event_container_t);
	if (cur_next > buf_end) {
		return -ENOMEM;
	}

	 /* Copy the TCG_EfiSpecIDEventStruct container template. */
	(void)memcpy(cur, (const void *)&id_event_templ, sizeof(id_event_templ));
	id_event = cur;

	/* TCG_EfiSpecIDEventStruct.numberOfAlgorithms */
	((id_event_container_t *)cur)->
	     id_event_misc_data.number_of_algorithms = num_algs;

	cur = cur_next;

	/* TCG_EfiSpecIDEventStruct.digestSizes[] */
	for (int i = 0; i < num_algs; i++) {
		cur_next = cur + sizeof(id_event_alg_info_t);
		if (cur_next > buf_end) {
			return -ENOMEM;
		}

		((id_event_alg_info_t *)cur)->algorithm_id = alg[i];
		((id_event_alg_info_t *)cur)->digest_size = tpm_alg_dsize(alg[i]);
		cur = cur_next;
	}

	#define VENDOR_INFO_SIZE  3U
	cur_next = cur + offsetof(id_event_vendor_data_t, vendor_info) +
	           VENDOR_INFO_SIZE;
	if (cur_next > buf_end) {
		return -ENOMEM;
	}

	/*
	 * TCG_EfiSpecIDEventStruct.vendorInfoSize -- vendor data is not supported
	 * currently.
	 * Note that when supporting vendor data, it is recommended that only
	 * 4-byte-aligned sizes are supported, because other sizes break the
	 * alignment assumptions relied upon when writing to the event log.
	 */
	((id_event_vendor_data_t *)cur)->vendor_info_size = VENDOR_INFO_SIZE;
	for (int i = 0; i < VENDOR_INFO_SIZE; i++) {
		((id_event_vendor_data_t *)cur)->vendor_info[i] = 0;
	}

	cur = cur_next;

	/* TCG_EfiSpecIDEventStruct container info. */
	((id_event_container_t *)id_event)->container.event_size =
	    cur - id_event - sizeof(((id_event_container_t *)NULL)->container);

	log_out->buf = (char *)buf;
	log_out->buf_bytes = buf_bytes;
	log_out->cursor = cur;
	log_out->startup_locality_event_data = NULL;
	return 0;
}

static const id_event_misc_data_t *tpm_log_get_id_event(
                                       const struct tpm_log_info *log)
{
	const char *const buf_end = log->buf + log->buf_bytes;

	if (log->buf + sizeof(id_event_misc_data_t) > buf_end) {
		return NULL;
	}

	return &((id_event_container_t *)log->buf)->id_event_misc_data;
}

static struct tpm_log_digest *digests_arg_get_digest(
                                  struct tpm_log_digests *digests,
                                  enum tpm_hash_alg required_h_alg)
{
	for (int i = 0; i < digests->count; i++) {
		struct tpm_log_digest *d = digests->d + i;

		if (d->h_alg == required_h_alg) {
			return d;
		}
	}

	return NULL;
}

static int add_tpml_digest_values(const struct tpm_log_info *log, char *cur,
                                  struct tpm_log_digests *digests,
                                  char **cur_out)
{
	const id_event_misc_data_t *id_event;
	const char *const buf_end = log->buf + log->buf_bytes;
	char *cur_next;

	if (!(id_event = tpm_log_get_id_event(log))) {
		return -EINVAL;
	}

	cur_next = cur + offsetof(tpml_digest_values_t, digests);
	if (cur_next > buf_end) {
		return -ENOMEM;
	}

	/* TCG_PCR_EVENT2.Digests.Count */
	((tpml_digest_values_t *)cur)->count = id_event->number_of_algorithms;
	cur = cur_next;

	/* TCG_PCR_EVENT2.Digests.Digests[] */
	for (int i = 0; i < id_event->number_of_algorithms; i++) {
		const id_event_alg_info_t *required_d = id_event->digest_sizes + i;
		struct tpm_log_digest *d;

		cur_next = cur + offsetof(tpmt_ha_t, digest);
		if (cur_next > buf_end) {
			return -ENOMEM;
		}

		/* TCG_PCR_EVENT2.Digests.Digests.Algorithm_Id */
		((tpmt_ha_t *)cur)->algorithm_id = required_d->algorithm_id;
		cur = cur_next;

		cur_next = cur + required_d->digest_size;
		if (cur_next > buf_end) {
			return -ENOMEM;
		}

		/* TCG_PCR_EVENT2.Digests.Digests.Digest */
		if (digests) {
			d = digests_arg_get_digest(digests, required_d->algorithm_id);
			(void)memcpy(cur, d->buf, required_d->digest_size);
		} else {
			(void)memset(cur, 0, required_d->digest_size);
		}

		cur = cur_next;
	}

	*cur_out = cur;
	return 0;
}

static int add_startup_locality_event2(const struct tpm_log_info *log, char *cur,
                                       uint8_t startup_locality,
                                       char **cur_out)
{
	const char *const buf_end = log->buf + log->buf_bytes;
	char *cur_next;
	int rc;

	cur_next = cur + offsetof(event2_header_t, digests);
	if (cur_next > buf_end) {
		return -ENOMEM;
	}

	/* Copy Startup Locality event container */
	(void)memcpy(cur, &startup_event_container_templ, cur_next - cur);
	cur = cur_next;

	if ((rc = add_tpml_digest_values(log, cur, NULL, &cur))) {
		return rc;
	}

	cur_next = cur + sizeof(startup_locality_event_t);
	if (cur_next > buf_end) {
		return -ENOMEM;
	}

	/* Copy TCG_EfiStartupLocalityEvent event */
	(void)memcpy(cur, &startup_event_templ, sizeof(startup_locality_event_t));

	/* Adjust TCG_EfiStartupLocalityEvent.StartupLocality */
	((startup_locality_event_t *)cur)->
			startup_event_data.startup_locality = startup_locality;

	cur = cur_next;

	*cur_out = cur;
	return 0;
}

static int check_arg_event_type(uint32_t event_type, enum tpm_pcr_idx pcr,
                       struct tpm_log_digests *digests,
                       const unsigned char *event_data, size_t event_data_bytes)
{
	/*
	 * As per TCG specifications, firmware components that are measured
	 * into PCR[0] must be logged in the event log using the event type
	 * EV_POST_CODE.
	 */
	if (pcr == TPM_PCR_0 && event_type != TPM_LOG_EV_POST_CODE) {
		return -EINVAL;
	}
	/*
	 * EV_NO_ACTION have digest byte values 0s for each allocated hash alg.
	 *
	 * Ref. Section 9.4.5 "EV_NO_ACTION Event Types", requirement #3.
	 */
	if (event_type == TPM_LOG_EV_NO_ACTION && digests) {
		return -EINVAL;
	}
	if (event_type != TPM_LOG_EV_NO_ACTION && !digests) {
		return -EINVAL;
	}
	/*
	 * TODO: Further event-specific validation or exceptions, e.g. as per
	 * Section 9.4 "Event Descriptions":
	 * - EV_ACTION
	 * - EV_EFI_ACTION
	 */

	return 0;
}

static int check_arg_digests(const id_event_misc_data_t *id_event,
                             struct tpm_log_digests *digests)
{
	/* Check that the digests being added fit the event log's structure. */

	if (digests->count != id_event->number_of_algorithms) {
		return -EINVAL;
	}

	for (int i = 0; i < digests->count; i++) {
		struct tpm_log_digest *d = digests->d + i;

		if (!tpm_alg_is_valid(d->h_alg)) {
			return -EINVAL;
		} else if (d->buf_bytes < tpm_alg_dsize(d->h_alg)) {
			return -EINVAL;
		}
	}

	for (int i = 0; i < id_event->number_of_algorithms; i++) {
		const id_event_alg_info_t *required_d = id_event->digest_sizes + i;

		if (!digests_arg_get_digest(digests, required_d->algorithm_id)) {
			return -EINVAL;
		}
	}

	return 0;
}

int tpm_log_add_event(struct tpm_log_info *log,
                      uint32_t event_type, enum tpm_pcr_idx pcr,
                      struct tpm_log_digests *digests,
                      const unsigned char *event_data, size_t event_data_bytes)
{
	const id_event_misc_data_t *id_event;
	const char *const buf_end = log->buf + log->buf_bytes;
	char *cur = log->cursor, *cur_next;
	int rc;

	if ((rc = check_arg_event_type(event_type, pcr, digests,
	                               event_data, event_data_bytes))) {
		return rc;
	}

	if (!(id_event = tpm_log_get_id_event(log))) {
		return -EINVAL;
	}

	if (digests && (rc = check_arg_digests(id_event, digests))) {
		return rc;
	}

	/*
	 * The Startup Locality event should be placed in the log before
	 * any event that extends PCR[0].
	 *
	 * Ref. TCG PC Client Platform Firmware Profile 9.4.5.3
	 */
	if (pcr == TPM_PCR_0 && log->startup_locality_event_data == NULL) {
		if ((rc = add_startup_locality_event2(log, cur, 3, &cur))) {
			return rc;
		}
	}

	cur_next = cur + offsetof(event2_header_t, digests);
	if (cur_next > buf_end) {
		return -ENOMEM;
	}

	/* TCG_PCR_EVENT2.PCRIndex */
	((event2_header_t *)cur)->pcr_index = pcr;

	/* TCG_PCR_EVENT2.EventType */
	((event2_header_t *)cur)->event_type = event_type;

	cur = cur_next;

	/*
	 * TODO: Further event-specific handling, e.g. as per Section 9.4 "Event
	 * Descriptions":
	 * - EV_ACTION
	 * - EV_EFI_ACTION
	 */

	/* TCG_PCR_EVENT2.Digests */
	if ((rc = add_tpml_digest_values(log, cur, digests, &cur))) {
		return rc;
	}

	cur_next = cur + offsetof(event2_data_t, event);
	if (cur_next > buf_end) {
		return -ENOMEM;
	}

	/* TCG_PCR_EVENT2.EventSize */
	((event2_data_t *)cur)->event_size = event_data_bytes;
	cur = cur_next;

	/* End of event data */
	cur_next = cur + event_data_bytes;
	if (cur_next > buf_end) {
		return -ENOMEM;
	}

	/* TCG_PCR_EVENT2.Event */
	(void)memcpy(cur, event_data, event_data_bytes);
	cur = cur_next;

	log->cursor = cur;

	return 0;
}

void tpm_log_serialise(char *dst, const struct tpm_log_info *log,
                       size_t *log_size_out)
{
	size_t log_size = log->cursor - log->buf;

	if (dst) {
		(void)memcpy(dst, log->buf, log_size);
	}
	if (log_size_out) {
		*log_size_out = log_size;
	}
}
