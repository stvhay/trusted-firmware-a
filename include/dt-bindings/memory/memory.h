/*
 * Copyright (c) 2021, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * This header provides constants for Memory Region attributes.
 */

#ifndef _DT_BINDINGS_MEMORY_H
#define _DT_BINDINGS_MEMORY_H

#define MEM_CODE	(0)
#define MEM_RO_DATA	(1)
#define MEM_RW_DATA	(2)
#define MEM_RO		(3)
#define MEM_RW		(4)

#define MEM_SECURE	(0)
#define MEM_NON_SECURE	(1)

#define MEM_DEVICE	(0)
#define MEM_NON_CACHE	(1)
#define MEM_NORMAL	(2)

#endif
