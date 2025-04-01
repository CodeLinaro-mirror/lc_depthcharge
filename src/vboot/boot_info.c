/*
 * Copyright 2015 Google LLC
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <assert.h>
#include <gpt.h>
#include <libpayload.h>
#include <stdint.h>
#include <stdlib.h>
#include <tss_constants.h>
#include <vb2_android_bootimg.h>
#include <tss_constants.h>

#include "base/gpt.h"
#include "base/string_utils.h"
#include "boot/android_bootconfig_params.h"
#include "boot/android_pvmfw.h"
#include "boot/android_bootconfig_params.h"
#include "boot/android_pvmfw.h"
#include "boot/bootconfig.h"
#include "boot/commandline.h"
#include "boot/multiboot.h"
#include "drivers/storage/blockdev.h"
#include "vboot/boot.h"
#include "vboot/boot_info.h"
#include "vboot/secdata_tpm.h"

/************************* CrOS Image Parsing ****************************/

static int fill_info_cros(struct boot_info *bi,
			  struct vb2_kernel_params *kparams)
{
	bi->kernel = kparams->kernel_buffer;
	bi->loader = (uint8_t *)bi->kernel + kparams->bootloader_offset;
	bi->params = (uint8_t *)bi->loader - CrosParamSize;
	bi->cmd_line = (char *)bi->params - CmdLineSize;

	return 0;
}

/*********************** Multiboot Image Parsing *************************/
#if CONFIG(KERNEL_MULTIBOOT)
static int fill_info_multiboot(struct boot_info *bi,
			       struct vb2_kernel_params *kparams)
{
	bi->kparams = kparams;

	if (multiboot_fill_boot_info(bi) < 0)
		return -1;

	return 0;
}
#endif

// Legacy Android boot
#define ANDROID_GKI_BOOT_HDR_SIZE 4096
#define ANDROID_BDEV_KEY_STR "androidboot.boot_devices"
#define ANDROID_BOOT_A_PART_NUM 13
#define ANDROID_BOOT_B_PART_NUM 14
#define ANDROID_VBMETA_A_PART_NUM 15
#define ANDROID_VBMETA_B_PART_NUM 16
#define ANDROID_SLOT_SUFFIX_KEY_STR "androidboot.slot_suffix"
#define ANDROID_FORCE_NORMAL_BOOT_KEY_STR "androidboot.force_normal_boot"

static int setup_pvmfw(struct boot_info *bi, struct vb2_kernel_params *kparams);

/*
 * Update cmdline with proper slot_suffix parameter
 */
static int modify_android_slot_suffix(struct bootconfig *bc,
				      struct vb2_kernel_params *kparams)
{
	char *str_to_insert;
	uint32_t partition_number = kparams->partition_number;


	/* Validate partition number according to supported layout at
	 * al-internal/platform/vendor/google_devices/houdini/+/tm-al:layout/disk_layout.json */
	if (partition_number == ANDROID_BOOT_A_PART_NUM ||
	    partition_number == ANDROID_VBMETA_A_PART_NUM) {
		str_to_insert = GPT_ENT_NAME_ANDROID_A_SUFFIX;
	} else if (partition_number == ANDROID_BOOT_B_PART_NUM ||
		   partition_number == ANDROID_VBMETA_B_PART_NUM) {
		str_to_insert = GPT_ENT_NAME_ANDROID_B_SUFFIX;
	} else {
		 /* Exit early if the partition_number is invalid */
		printf("Unsupported partition number to boot GKI: %d\n",
		       partition_number);
		return -1;
	}

	return bootconfig_append(bc, ANDROID_SLOT_SUFFIX_KEY_STR, str_to_insert);
}

/*
 * Update bootconfig with proper force_normal_boot parameter
 */
static int modify_android_force_normal_boot(struct bootconfig *bc,
					    bool recovery_boot)
{
	char *str_to_insert;

	str_to_insert = recovery_boot ? "0" : "1";

	return bootconfig_append(bc, ANDROID_FORCE_NORMAL_BOOT_KEY_STR, str_to_insert);
}

static bool gki_is_recovery_boot(struct vb2_kernel_params *kparams)
{
	switch (kparams->boot_command) {
	case VB2_BOOT_CMD_NORMAL_BOOT:
		return false;

	case VB2_BOOT_CMD_BOOTLOADER_BOOT:
		/*
		 * TODO(b/358088653): We should enter fastboot mode and clear
		 * BCB command in misc partition. For now ignore that and boot
		 * to recovery where fastbootd should be available.
		 */
		return true;

	case VB2_BOOT_CMD_RECOVERY_BOOT:
		return true;

	default:
		printf("Unknown boot command, assume recovery boot is required\n");
		return true;
	}
}

static bool gki_ramdisk_fragment_needed(struct vendor_ramdisk_table_entry_v4 *fragment,
					bool recovery_boot)
{
	/* Ignore all other properties except ramdisk type */
	switch (fragment->ramdisk_type) {
	case VENDOR_RAMDISK_TYPE_PLATFORM:
	case VENDOR_RAMDISK_TYPE_DLKM:
		return true;

	case VENDOR_RAMDISK_TYPE_RECOVERY:
		return recovery_boot;

	default:
		printf("Unknown ramdisk type 0x%x\n", fragment->ramdisk_type);

		return false;
	}
}

static int legacy_gki_setup_ramdisk(struct boot_info *bi,
			     struct vb2_kernel_params *kparams,
			     int fill_cmdline)
{
	struct vendor_boot_img_hdr_v4 *vendor_hdr;
	struct boot_img_hdr_v4 *init_hdr;
	struct bootconfig_trailer *trailer = NULL;
	uint8_t *init_boot_ramdisk_src;
	uint8_t *vendor_ramdisk;
	uint8_t *vendor_ramdisk_end;
	uint32_t vendor_ramdisk_table_section_offset;
	uint32_t vendor_ramdisk_section_offset;
	uint32_t bootconfig_section_offset;
	uintptr_t bootc_ramdisk_addr;
	bool recovery_boot;
	struct bootconfig bc;
	int ret;

	vendor_hdr = (struct vendor_boot_img_hdr_v4 *)((uintptr_t)kparams->kernel_buffer +
						       kparams->vendor_boot_offset);
	init_hdr = (struct boot_img_hdr_v4 *)((uintptr_t)kparams->kernel_buffer +
					      kparams->init_boot_offset);

	if (init_hdr->kernel_size != 0) {
		printf("GKI: Kernel size on init_boot partition has to be zero\n");
		return -1;
	}

	/* Calculate address offset of vendor_ramdisk section on vendor_boot partition */
	vendor_ramdisk_section_offset = ALIGN_UP(sizeof(struct vendor_boot_img_hdr_v4),
						 vendor_hdr->page_size);
	vendor_ramdisk_table_section_offset = vendor_ramdisk_section_offset +
		ALIGN_UP(vendor_hdr->vendor_ramdisk_size, vendor_hdr->page_size) +
		ALIGN_UP(vendor_hdr->dtb_size, vendor_hdr->page_size);

	/* Check if vendor ramdisk table is correct */
	if (vendor_hdr->vendor_ramdisk_table_size <
	    vendor_hdr->vendor_ramdisk_table_entry_num *
	    vendor_hdr->vendor_ramdisk_table_entry_size) {
		printf("GKI: Too small vendor ramdisk table\n");
		return -1;
	}

	recovery_boot = gki_is_recovery_boot(kparams);

	vendor_ramdisk = (uint8_t *)vendor_hdr + vendor_ramdisk_section_offset;
	vendor_ramdisk_end = vendor_ramdisk;

	/* Go through all ramdisk fragments and keep only the required ones */
	for (uintptr_t i = 0,
	     fragment_ptr = (uintptr_t)vendor_hdr + vendor_ramdisk_table_section_offset;
	     i < vendor_hdr->vendor_ramdisk_table_entry_num;
	     fragment_ptr += vendor_hdr->vendor_ramdisk_table_entry_size, i++) {

		struct vendor_ramdisk_table_entry_v4 *fragment;
		uint8_t *fragment_src;

		fragment = (struct vendor_ramdisk_table_entry_v4 *)fragment_ptr;
		if (!gki_ramdisk_fragment_needed(fragment, recovery_boot))
			/* Fragment not needed, skip it */
			continue;

		fragment_src = vendor_ramdisk + fragment->ramdisk_offset;
		if (vendor_ramdisk_end != fragment_src)
			/*
			 * A fragment was skipped before, we need to move current one
			 * at the correct place.
			 */
			memmove(vendor_ramdisk_end, fragment_src, fragment->ramdisk_size);

		/* Update location of the end of vendor ramdisk */
		vendor_ramdisk_end += fragment->ramdisk_size;
	}

	if (CONFIG(BOOTCONFIG)) {
		/* Calculate offset of bootconfig section */
		bootconfig_section_offset = vendor_ramdisk_table_section_offset +
			ALIGN_UP(vendor_hdr->vendor_ramdisk_table_size,
				 vendor_hdr->page_size);

		/* Put bootconfig right after ramdisks */
		bootc_ramdisk_addr = (uintptr_t)vendor_ramdisk_end + init_hdr->ramdisk_size;

		if ((bootc_ramdisk_addr + vendor_hdr->bootconfig_size) >=
		    ((uintptr_t)kparams->kernel_buffer + kparams->kernel_buffer_size)) {
			printf("GKI: Not enough space for bootconfig\n");
			return -1;
		}

		uintptr_t kernel_buffer_end = (uintptr_t)kparams->kernel_buffer +
					      kparams->kernel_buffer_size;
		bootconfig_init(&bc, (void *)bootc_ramdisk_addr,
		   kernel_buffer_end - bootc_ramdisk_addr);

		/* Generate valid (that is including trailer) bootconfig section
		 * at the end of a ramdisk. Keep track of its size which is
		 * necessary in case of updating it later on.
		 */
		ret = bootconfig_append_params(&bc,
					      (uint8_t *)vendor_hdr + bootconfig_section_offset,
					       vendor_hdr->bootconfig_size);
		if (ret < 0) {
			printf("GKI: Cannot parse build time bootconfig\n");
			return -1;
		}

		ret = bootconfig_append_cmdline(&bc, kparams->kernel_bootconfig_buffer);
		if (ret < 0) {
			printf("GKI: Cannot copy avb cmdline to bootconfig\n");
			return -1;
		}

		if (append_android_bootconfig_params(&bc, kparams) < 0)
			/*
			 * On error, just log a message and continue with the rest of the
			 * bootflow. The  idea is to get as many run-time bootconfig params
			 * filled up as possible without halting the bootflow and let the OS
			 * decide rather than FW blocking the boot to OS.
			 */
			printf("GKI: Cannot append all android bootconfig params\n");


		/* Update slot suffix */
		if (modify_android_slot_suffix(&bc, kparams))
			return -1;

		/* Select boot mode */
		if (modify_android_force_normal_boot(&bc, recovery_boot))
			return -1;

		trailer = bootconfig_finalize(&bc,
			sizeof(BOOTCONFIG_BOOTTIME_KEY_STR "=" BOOTCONFIG_MAX_BOOTTIME_STR) +
			sizeof(BOOTCONFIG_DELIMITER));
	}

	commandline_append(kparams->kernel_cmdline_buffer);

	/* On init_boot there's no kernel, so ramdisk follows the header */
	init_boot_ramdisk_src = (uint8_t *)init_hdr + ANDROID_GKI_BOOT_HDR_SIZE;

	/* Move init_boot ramdisk to directly follow the vendor_boot ramdisk.
	 * This is a requirement from Android system. The cpio/gzip/lz4
	 * compression formats support this type of concatenation. After
	 * the kernel decompresses, it extracts contatenated file into
	 * an initramfs, which results in a file structure that's a generic
	 * ramdisk (from init_boot) overlaid on the vendor ramdisk (from
	 * vendor_boot) file structure. */
	memmove(vendor_ramdisk_end, init_boot_ramdisk_src, init_hdr->ramdisk_size);

	/* Update ramdisk addr and size */
	bi->ramdisk_addr = vendor_ramdisk;
	bi->ramdisk_size = (uint8_t *)(trailer + 1) - vendor_ramdisk;

	if (fill_cmdline)
		bi->cmd_line = (char *)vendor_hdr->cmdline;

	return 0;
}

static int legacy_fill_info_gki(struct boot_info *bi,
			 struct vb2_kernel_params *kparams)
{
	if (kparams->kernel_buffer == NULL) {
		printf("Pointer to kernel buffer is not initialized\n");
		return -1;
	}

	if (CONFIG(ANDROID_PVMFW))
		if (setup_pvmfw(bi, kparams))
			printf("Failed to setup pvmfw\n");

	/* Kernel starts at the beginning of kernel buffer */
	bi->kernel = kparams->kernel_buffer;

	if (legacy_gki_setup_ramdisk(bi, kparams, 1))
		return -1;

	return 0;
}

/****************************** Android GKI ******************************/

static int gki_setup_bootconfig(struct boot_info *bi, struct vb2_kernel_params *kp)
{
	struct bootconfig_trailer *trailer;
	struct bootconfig bc;
	int ret;

	uintptr_t kernel_buffer_end = (uintptr_t)kp->kernel_buffer + kp->kernel_buffer_size;
	uintptr_t ramdisk_end = (uintptr_t)kp->ramdisk + kp->ramdisk_size;
	bootconfig_init(&bc, (void *)ramdisk_end, kernel_buffer_end - ramdisk_end);

	/*
	 * "bootconfig" is already included in the vendor_boot cmdline, if the
	 * vendor ramdisk contains non-empty bootconfig. Since in our case we're
	 * unconditionally adding some parameters this way, we'll need to make
	 * sure that kernel knows to parse them even if there's nothing
	 * in the vendor ramdisk.
	 */
	if (!kp->bootconfig_size)
		commandline_append("bootconfig");

	/* Append parameters from vendor image to bootconfig */
	ret = bootconfig_append_params(&bc, kp->bootconfig, kp->bootconfig_size);
	if (ret < 0) {
		printf("GKI: Cannot append build time bootconfig\n");
		return -1;
	}

	ret = bootconfig_append_cmdline(&bc, kp->vboot_cmdline_buffer);
	if (ret < 0) {
		printf("GKI: Cannot copy vboot cmdline to bootconfig\n");
		return -1;
	}

	append_android_bootconfig_params(&bc, kp);

	trailer = bootconfig_finalize(&bc,
			sizeof(BOOTCONFIG_BOOTTIME_KEY_STR "=" BOOTCONFIG_MAX_BOOTTIME_STR) +
			sizeof(BOOTCONFIG_DELIMITER));
	if (!trailer) {
		printf("GKI: Cannot finalize bootconfig\n");
		return -1;
	}

	/* Update ramdisk size after adding bootconfig */
	bi->ramdisk_size += trailer->params_size + sizeof(*trailer);

	return 0;
}

/*
 * Fill struct boot_info with pvmfw information and fill pvmfw config.
 */
static int setup_pvmfw(struct boot_info *bi, struct vb2_kernel_params *kparams)
{
	int ret;
	uint32_t status;
	size_t pvmfw_size = kparams->pvmfw_out_size, params_size;
	void *pvmfw_addr = kparams->pvmfw_buffer, *params = NULL;

	if (!pvmfw_addr || pvmfw_size == 0) {
		/* There is no pvmfw so fail and don't do anything */
		printf("pvmfw was not loaded\n");
		return -1;
	}

	/* Get pvmfw boot params from GSC */
	status = secdata_get_pvmfw_params(&params, &params_size);
	if (status != TPM_SUCCESS) {
		printf("Failed to get pvmfw gsc boot params data. "
		       "secdata_get_pvmfw_params returned %u\n", status);
		ret = -1;
		goto fail;
	}

	/* Verify that pvmfw start address is aligned */
	if (!IS_ALIGNED((uintptr_t)pvmfw_addr, ANDROID_PVMFW_CFG_ALIGN)) {
		printf("Failed to setup pvmfw at aligned address\n");
		ret = -1;
		goto fail;
	}

	ret = setup_android_pvmfw(pvmfw_addr,
				  kparams->pvmfw_buffer_size,
				  &pvmfw_size, params, params_size);
	if (ret != 0) {
		printf("Failed to setup pvmfw configuration\n");
		goto fail;
	}

	bi->pvmfw_addr = pvmfw_addr;
	bi->pvmfw_size = pvmfw_size;
fail:
	/* TODO(b/380002393): Clear the remains before jumping to kernel */
	if (params) {
		/* Make sure that secrets are no longer in memory */
		memset(params, 0, params_size);
		free(params);
	}

	/* If failed then clear the buffer */
	if (ret != 0)
		memset(kparams->pvmfw_buffer, 0, kparams->pvmfw_buffer_size);

	return ret;
}

static int fill_info_gki(struct boot_info *bi,
			 struct vb2_kernel_params *kparams)
{
	if (kparams->kernel_buffer == NULL) {
		printf("Pointer to kernel buffer is not initialized\n");
		return -1;
	}

	/* gki_setup_bootconfig() expects ramdisk to be part of kernel buffer */
	assert((void *)kparams->ramdisk > kparams->kernel_buffer &&
	       kparams->ramdisk + kparams->ramdisk_size <=
	       (uint8_t *)kparams->kernel_buffer + kparams->kernel_buffer_size);

	/* Kernel starts at the beginning of kernel buffer */
	bi->kernel = kparams->kernel_buffer + BOOT_HEADER_SIZE;
	bi->ramdisk_addr = kparams->ramdisk;
	bi->ramdisk_size = kparams->ramdisk_size;
	bi->cmd_line = kparams->vendor_cmdline_buffer;

	if (CONFIG(BOOTCONFIG)) {
		if (gki_setup_bootconfig(bi, kparams))
			return -1;
	}

	if (CONFIG(ANDROID_PVMFW)) {
		if (setup_pvmfw(bi, kparams))
			printf("Failed to setup pvmfw\n");
	}

	return 0;
}

int fill_boot_info(struct boot_info *bi, struct vb2_kernel_params *kparams)
{
	uint32_t type = GET_KERNEL_IMG_TYPE(kparams->flags);

	if (type == KERNEL_IMAGE_CROS) {
		return fill_info_cros(bi, kparams);
	} else if (type == KERNEL_IMAGE_BOOTIMG) {
		printf("Boot Android via BOOTIMG type\n");
		return fill_info_gki(bi, kparams);
#if CONFIG(KERNEL_MULTIBOOT)
	} else if (type == KERNEL_IMAGE_MULTIBOOT) {
		return fill_info_multiboot(bi, kparams);
#endif
	} else if (type == KERNEL_IMAGE_ANDROID_GKI) {
		printf("Boot Android via ANDROID_GKI type\n");
		return legacy_fill_info_gki(bi, kparams);
	} else {
		printf("%s: Invalid image type %x!\n", __func__, type);
		return -1;
	}
}
