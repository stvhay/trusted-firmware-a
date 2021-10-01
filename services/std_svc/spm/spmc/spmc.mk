#
# Copyright (c) 2021, ARM Limited and Contributors. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#

ifneq (${ARCH},aarch64)
        $(error "Error: SPMC is only supported on aarch64.")
endif

SPMC_SOURCES	:=	$(addprefix services/std_svc/spm/spmc/,	\
			spmc_main.c				\
			spmc_setup.c				\
			spmc_pm.c				\
			spmc_shared_mem.c			\
			logical_sp_test.c)


# Let the top-level Makefile know that we intend to include a BL32 image
NEED_BL32		:=	yes

# Enable save and restore for non-secure timer register
NS_TIMER_SWITCH		:=	1

# The SPMC is paired with a Test Secure Payload source and we intend to
# build the Test Secure Payload along with this dispatcher.
#
# In cases where an associated Secure Payload lies outside this build
# system/source tree, the the dispatcher Makefile can either invoke an external
# build command or assume it pre-built

BL32_ROOT		:=	bl32/tsp

# Include SP's Makefile. The assumption is that the TSP's build system is
# compatible with that of Trusted Firmware, and it'll add and populate necessary
# build targets and variables
include ${BL32_ROOT}/tsp.mk
