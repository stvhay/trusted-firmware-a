/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * DRTM support for DRTM error remediation.
 *
 */
#include <stdint.h>

#include <common/debug.h>
#include <common/runtime_svc.h>

#include "drtm_main.h"


static enum drtm_retc drtm_error_set(long long error_code)
{
	/* TODO: Store the error code in non-volatile memory. */

	return SUCCESS;
}

static enum drtm_retc drtm_error_get(long long *error_code)
{
	/* TODO: Get error code from non-volatile memory. */

	*error_code = 0;

	return SUCCESS;
}

void drtm_enter_remediation(long long err_code, const char *err_str)
{
	int rc;

	if ((rc = drtm_error_set(err_code))) {
		ERROR("%s(): drtm_error_set() failed unexpectedly rc=%d\n",
		      __func__, rc);
		panic();
	}

	NOTICE("DRTM: entering remediation of error:\n%lld\t\'%s\'\n",
	       err_code, err_str);

	/* TODO: Reset the system rather than panic(). */
	ERROR("%s(): system reset is not yet supported\n", __func__);
	panic();
}

uintptr_t drtm_set_error(uint64_t x1, void *ctx)
{
	int rc;

	if ((rc = drtm_error_set(x1))) {
		SMC_RET1(ctx, rc);
	}

	SMC_RET1(ctx, SUCCESS);
}

uintptr_t drtm_get_error(void *ctx)
{
	long long error_code;
	int rc;

	if ((rc = drtm_error_get(&error_code))) {
		SMC_RET1(ctx, rc);
	}

	SMC_RET2(ctx, SUCCESS, error_code);
}
