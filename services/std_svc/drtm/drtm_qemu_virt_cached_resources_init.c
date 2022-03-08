/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * DRTM protected resources
 */

#include "drtm_main.h"
#include "drtm_cache.h"
#include "drtm_dma_prot.h"

/*
 * XXX Note: the generic protected DRTM resources are being specialised into
 * DRTM TCB hashes.  Platform resources retrieved through the generic DRTM cache
 * are going to be retrieved through bespoke interfaces instead.
 * This file and drtm_cache.c will be removed once the transition is complete.
 */

struct __packed __descr_table_n {
	struct_drtm_mem_region_descr_table header;
	struct_drtm_mem_region_descr regions[24];
};

static const struct __descr_table_n qemu_virt_address_map = {
    .header = {
        .version = 1,
        .num_regions = sizeof(((struct __descr_table_n *)NULL)->regions) /
                       sizeof(((struct __descr_table_n *)NULL)->regions[0])
    },
    /* See qemu/hw/arm/virt.c :
     *
     * static const MemMapEntry base_memmap[] = {
     * 	// Space up to 0x8000000 is reserved for a boot ROM
     * 	[VIRT_FLASH] =              {          0, 0x08000000 },
     * 	[VIRT_CPUPERIPHS] =         { 0x08000000, 0x00020000 },
     * 	// GIC distributor and CPU interfaces sit inside the CPU peripheral space
     * 	[VIRT_GIC_DIST] =           { 0x08000000, 0x00010000 },
     * 	[VIRT_GIC_CPU] =            { 0x08010000, 0x00010000 },
     * 	[VIRT_GIC_V2M] =            { 0x08020000, 0x00001000 },
     * 	[VIRT_GIC_HYP] =            { 0x08030000, 0x00010000 },
     * 	[VIRT_GIC_VCPU] =           { 0x08040000, 0x00010000 },
     * 	// The space in between here is reserved for GICv3 CPU/vCPU/HYP
     * 	[VIRT_GIC_ITS] =            { 0x08080000, 0x00020000 },
     * 	// This redistributor space allows up to 2*64kB*123 CPUs
     * 	[VIRT_GIC_REDIST] =         { 0x080A0000, 0x00F60000 },
     * 	[VIRT_UART] =               { 0x09000000, 0x00001000 },
     * 	[VIRT_RTC] =                { 0x09010000, 0x00001000 },
     * 	[VIRT_FW_CFG] =             { 0x09020000, 0x00000018 },
     * 	[VIRT_GPIO] =               { 0x09030000, 0x00001000 },
     * 	[VIRT_SECURE_UART] =        { 0x09040000, 0x00001000 },
     * 	[VIRT_SMMU] =               { 0x09050000, 0x00020000 },
     * 	[VIRT_PCDIMM_ACPI] =        { 0x09070000, MEMORY_HOTPLUG_IO_LEN },
     * 	[VIRT_ACPI_GED] =           { 0x09080000, ACPI_GED_EVT_SEL_LEN },
     * 	[VIRT_NVDIMM_ACPI] =        { 0x09090000, NVDIMM_ACPI_IO_LEN},
     * 	[VIRT_PVTIME] =             { 0x090a0000, 0x00010000 },
     * 	[VIRT_SECURE_GPIO] =        { 0x090b0000, 0x00001000 },
     * 	[VIRT_MMIO] =               { 0x0a000000, 0x00000200 },
     * 	// ...repeating for a total of NUM_VIRTIO_TRANSPORTS, each of that size
     * 	[VIRT_PLATFORM_BUS] =       { 0x0c000000, 0x02000000 },
     * 	[VIRT_SECURE_MEM] =         { 0x0e000000, 0x01000000 },
     * 	[VIRT_PCIE_MMIO] =          { 0x10000000, 0x2eff0000 },
     * 	[VIRT_PCIE_PIO] =           { 0x3eff0000, 0x00010000 },
     * 	[VIRT_PCIE_ECAM] =          { 0x3f000000, 0x01000000 },
     * 	// Actual RAM size depends on initial RAM and device memory settings
     * 	[VIRT_MEM] =                { GiB, LEGACY_RAMLIMIT_BYTES },
     * };
     *
     * Note: When adjusting the regions below, please update the array length
     * in the __descr_table_n structure accordingly.
     *
     */
#define PAGES_AND_TYPE(bytes, type) \
	.pages_and_type = DRTM_MEM_REGION_PAGES_AND_TYPE( \
	                      (size_t)(bytes) / DRTM_PAGE_SIZE + \
	                       ((size_t)(bytes) % DRTM_PAGE_SIZE != 0), \
	                      DRTM_MEM_REGION_TYPE_##type)
	.regions = {
		{.paddr =          0, PAGES_AND_TYPE(0x08000000, NON_VOLATILE)},
		{.paddr = 0x08000000, PAGES_AND_TYPE(0x00021000, DEVICE)},
		{.paddr = 0x08030000, PAGES_AND_TYPE(0x00020000, DEVICE)},
		{.paddr = 0x08080000, PAGES_AND_TYPE(0x00F80000, DEVICE)},
		{.paddr = 0x09000000, PAGES_AND_TYPE(0x00001000, DEVICE)},
		{.paddr = 0x09010000, PAGES_AND_TYPE(0x00001000, DEVICE)},
		{.paddr = 0x09020000, PAGES_AND_TYPE(0x00000018, DEVICE)},
		{.paddr = 0x09030000, PAGES_AND_TYPE(0x00001000, DEVICE)},
		/* {.paddr = 0x09040000, PAGES_AND_TYPE(0x00001000, RESERVED)}, */
		{.paddr = 0x09050000, PAGES_AND_TYPE(0x00020000 + DRTM_PAGE_SIZE, DEVICE)},
		{.paddr = 0x09080000, PAGES_AND_TYPE(DRTM_PAGE_SIZE, DEVICE)},
		{.paddr = 0x09090000, PAGES_AND_TYPE(DRTM_PAGE_SIZE, DEVICE)},
		{.paddr = 0x090a0000, PAGES_AND_TYPE(0x00010000, DEVICE)},
		/* {.paddr = 0x090b0000, PAGES_AND_TYPE(0x00001000, RESERVED)}, */
		{.paddr = 0x0a000000, PAGES_AND_TYPE(0x00000200, DEVICE)},
		{.paddr = 0x0c000000, PAGES_AND_TYPE(0x02000000, DEVICE)},
		/* {.paddr = 0x0e000000, PAGES_AND_TYPE(0x01000000, RESERVED)}, */
		{.paddr = 0x10000000, PAGES_AND_TYPE(0x30000000, DEVICE)},
		/*
		 * At most 3 GiB RAM, to align with TF-A's max PA on ARM QEMU.
		 * Actual RAM size depends on initial RAM and device memory settings.
		 */
		{.paddr = 0x40000000, PAGES_AND_TYPE(0xc0000000 /* 3 GiB */, NORMAL)},
	},
#undef PAGES_AND_TYPE
};


static const struct cached_res CACHED_RESOURCES_INIT[] = {
    {
      .id = "address-map",
      .bytes = sizeof(qemu_virt_address_map),
      .data_ptr = (char *)&qemu_virt_address_map,
    },
};

#define CACHED_RESOURCES_INIT_END (CACHED_RESOURCES_INIT + \
        sizeof(CACHED_RESOURCES_INIT) / sizeof(CACHED_RESOURCES_INIT[0]))
