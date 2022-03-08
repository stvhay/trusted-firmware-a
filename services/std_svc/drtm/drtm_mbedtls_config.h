/*
 * Copyright (c) 2015-2020, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/*
 * Key algorithms currently supported on mbed TLS libraries
 */
#define TF_MBEDTLS_RSA			1
#define TF_MBEDTLS_ECDSA		2
#define TF_MBEDTLS_RSA_AND_ECDSA	3

#define TF_MBEDTLS_USE_RSA (TF_MBEDTLS_KEY_ALG_ID == TF_MBEDTLS_RSA \
		|| TF_MBEDTLS_KEY_ALG_ID == TF_MBEDTLS_RSA_AND_ECDSA)
#define TF_MBEDTLS_USE_ECDSA (TF_MBEDTLS_KEY_ALG_ID == TF_MBEDTLS_ECDSA \
		|| TF_MBEDTLS_KEY_ALG_ID == TF_MBEDTLS_RSA_AND_ECDSA)

/*
 * Hash algorithms currently supported on mbed TLS libraries
 */
#define TF_MBEDTLS_SHA256		1
#define TF_MBEDTLS_SHA384		2
#define TF_MBEDTLS_SHA512		3

/*
 * Configuration file to build mbed TLS with the required features for
 * Trusted Boot
 */

#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
/* Prevent mbed TLS from using snprintf so that it can use tf_snprintf. */
#define MBEDTLS_PLATFORM_SNPRINTF_ALT
#define MBEDTLS_PLATFORM_C

#define MBEDTLS_MEMORY_BUFFER_ALLOC_C

#if DRTM_SHA_ALG == 256
#define MBEDTLS_SHA256_C
#elif DRTM_SHA_ALG == 384 || DRTM_SHA_ALG == 512
#define MBEDTLS_SHA512_C
#else
#define MBEDTLS_SHA512_C
#endif
#define MBEDTLS_MD_C
#define MBEDTLS_ERROR_C
#define MBEDTLS_VERSION_C

/* Memory buffer allocator options */
#define MBEDTLS_MEMORY_ALIGN_MULTIPLE		8

/*
 * Prevent the use of 128-bit division which
 * creates dependency on external libraries.
 */
#define MBEDTLS_NO_UDBL_DIVISION

#ifndef __ASSEMBLER__
/* System headers required to build mbed TLS with the current configuration */
#include <stdlib.h>
#include <mbedtls/check_config.h>
#endif

#define TF_MBEDTLS_HEAP_SIZE		U(4 * 1024)

#endif /* MBEDTLS_CONFIG_H */
