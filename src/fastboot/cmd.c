/*
 * Copyright 2021 Google LLC
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

#include <stdlib.h>
#include <string.h>
#include <vb2_android_misc.h>

#include "fastboot/cmd.h"
#include "fastboot/disk.h"
#include "fastboot/fastboot.h"
#include "fastboot/vars.h"
#include "gpt_misc.h"

static int parse_hex(const char *str, uint32_t *ret)
{
	char c;
	int valid = 0;
	uint32_t result = 0;

	while ((c = *str++)) {
		int units = 0;
		if (c >= '0' && c <= '9') {
			units = c - '0';
		} else if (c >= 'A' && c <= 'F') {
			units = 10 + (c - 'A');
		} else if (c >= 'a' && c <= 'f') {
			units = 10 + (c - 'a');
		} else {
			break;
		}

		result *= 0x10;
		result += units;
		valid++;
	}

	*ret = result;

	return valid;
}

static void fastboot_cmd_continue(struct FastbootOps *fb, const char *arg)
{
	fastboot_okay(fb, "Continuing boot");
	fb->state = FINISHED;
}

static void fastboot_cmd_download(struct FastbootOps *fb, const char *arg)
{
	uint32_t size = 0;
	int digits = parse_hex(arg, &size);
	if (arg[digits] != '\0') {
		fastboot_fail(fb, "Invalid argument");
		return;
	}

	if (size > FASTBOOT_MAX_DOWNLOAD_SIZE) {
		fastboot_fail(fb, "File too big");
		return;
	}

	fastboot_data(fb, size);
}

#define FASTBOOT_RAW_WRITE_ARG "raw-sector:"
#define FASTBOOT_RAW_WRITE_ARG_LEN (sizeof(FASTBOOT_RAW_WRITE_ARG) - 1)

static void fastboot_cmd_flash(struct FastbootOps *fb, const char *arg)
{
	struct fastboot_disk disk;
	if (!fb->has_download) {
		fastboot_fail(fb, "No data staged to flash");
		return;
	}

	uint64_t data_len;
	void *data = fastboot_get_download_buffer(fb, &data_len);

	if (!strncmp(arg, FASTBOOT_RAW_WRITE_ARG, FASTBOOT_RAW_WRITE_ARG_LEN)) {
		long long int offset = strtoll(arg + FASTBOOT_RAW_WRITE_ARG_LEN, NULL, 0);
		if (offset < 0) {
			fastboot_fail(fb, "Offset cannot be negative");
			return;
		}
		disk.disk = NULL;
		fastboot_disk_init(&disk);
		/* Ignore errors with GPT as we don't need that for raw write */
		if (disk.disk == NULL) {
			fastboot_fail(fb, "Failed to init disk");
			return;
		}
		/*
		 * disk.gpt can be null if there is no valid GPT on disk. If disk.gpt
		 * is not null, free it so we will not overwrite GPT if GPT was
		 * modified by raw write.
		 */
		if (disk.gpt) {
			fastboot_disk_destroy(&disk);
		}
		if (disk.disk->block_count <= offset) {
			fastboot_fail(fb, "Offset cannot be larger then disk block count");
			return;
		}

		fastboot_write_raw(fb, &disk, (uint64_t)offset, disk.disk->block_count - offset,
				   data, (uint32_t)data_len);
		return;
	}

	if (!fastboot_disk_init(&disk)) {
		fastboot_fail(fb, "Failed to init disk");
		return;
	}

	fastboot_write(fb, &disk, arg, data, (uint32_t)data_len, 0);
	fastboot_disk_destroy(&disk);
}

static void fastboot_cmd_erase(struct FastbootOps *fb, const char *arg)
{
	struct fastboot_disk disk;
	if (!fastboot_disk_init(&disk)) {
		fastboot_fail(fb, "Failed to init disk");
		return;
	}

	fastboot_erase(fb, &disk, arg);
	fastboot_disk_destroy(&disk);
}

static bool fastboot_get_cmdline_offset(struct FastbootOps *fb,
					enum vb2_fastboot_cmdline_magic magic,
					size_t *offset)
{
	switch (magic) {
	case VB2_FASTBOOT_CMDLINE_MAGIC:
		*offset = VB2_MISC_VENDOR_SPACE_FASTBOOT_CMDLINE_OFFSET;
		return true;
	case VB2_FASTBOOT_BOOTCONFIG_MAGIC:
		*offset = VB2_MISC_VENDOR_SPACE_FASTBOOT_BOOTCONFIG_OFFSET;
		return true;
	default:
		FB_FAIL_AND_DEBUG(fb, "Unknown magic: 0x%x\n", magic);
		return false;
	}
}

static bool fastboot_read_misc_cmdline(struct FastbootOps *fb,
				       struct vb2_fastboot_cmdline *fb_cmd,
				       enum vb2_fastboot_cmdline_magic magic)
{
	struct fastboot_disk disk;
	size_t offset;
	size_t offset_from_buf = 0;
	size_t buffer_len;
	void *buffer;

	if (!fastboot_get_cmdline_offset(fb, magic, &offset))
		return false;

	if (!fastboot_disk_init(&disk)) {
		FB_FAIL_AND_DEBUG(fb, "Failed to init disk");
		return false;
	}

	buffer = fb_cmd;
	buffer_len = sizeof(struct vb2_fastboot_cmdline);
	/* Allocate block aligned buffer if size or offset isn't block aligned */
	if (buffer_len % disk.disk->block_size || offset % disk.disk->block_size) {
		offset_from_buf = offset % disk.disk->block_size;
		offset -= offset_from_buf;
		buffer_len += offset_from_buf;
		buffer_len = ALIGN_UP(buffer_len, disk.disk->block_size);
		buffer = malloc(buffer_len);
		if (buffer == NULL) {
			FB_FAIL_AND_DEBUG(fb, "memory allocation failed");
			fastboot_disk_destroy(&disk);
			return false;
		}
	}

	if (!fastboot_read(&disk, GPT_ENT_NAME_ANDROID_MISC, buffer,
			   buffer_len, offset)) {
		FB_FAIL_AND_DEBUG(fb, "Failed to read misc partition (magic 0x%x, offset %ld)",
				  magic, offset);
		free(buffer);
		fastboot_disk_destroy(&disk);
		return false;
	}

	fastboot_disk_destroy(&disk);

	if (fb_cmd != buffer) {
		memcpy(fb_cmd, buffer + offset_from_buf, sizeof(struct vb2_fastboot_cmdline));
		free(buffer);
	}

	if (!vb2_is_fastboot_cmdline_valid(fb_cmd, magic)) {
		FB_FAIL_AND_DEBUG(fb, "Invalid cmdline data stored in misc");
		return false;
	}

	return true;
}

static void fastboot_write_misc(struct FastbootOps *fb, size_t offset, void *data,
				size_t data_len)
{
	struct fastboot_disk disk;
	size_t offset_from_buf = 0;
	size_t buffer_len;
	void *buffer;

	if (!fastboot_disk_init(&disk)) {
		fastboot_fail(fb, "Failed to init disk");
		return;
	}

	buffer = data;
	buffer_len = data_len;
	/* Allocate block aligned buffer if size or offset isn't block aligned */
	if (buffer_len % disk.disk->block_size || offset % disk.disk->block_size) {
		offset_from_buf = offset % disk.disk->block_size;
		offset -= offset_from_buf;
		buffer_len += offset_from_buf;
		buffer_len = ALIGN_UP(buffer_len, disk.disk->block_size);
		buffer = malloc(buffer_len);
		if (buffer == NULL) {
			fastboot_fail(fb, "memory allocation failed");
			fastboot_disk_destroy(&disk);
			return;
		}
		if (!fastboot_read(&disk, GPT_ENT_NAME_ANDROID_MISC, buffer,
				   buffer_len, offset)) {
			fastboot_fail(fb, "Failed to read misc partition (offset %ld)",
				      offset);
			free(buffer);
			fastboot_disk_destroy(&disk);
			return;
		}
		memcpy(buffer + offset_from_buf, data, data_len);
	}

	fastboot_write(fb, &disk, GPT_ENT_NAME_ANDROID_MISC, buffer,
		       buffer_len, offset);

	if (data != buffer)
		free(buffer);

	fastboot_disk_destroy(&disk);
}

static void fastboot_write_misc_cmdline(struct FastbootOps *fb,
					struct vb2_fastboot_cmdline *fb_cmd,
					enum vb2_fastboot_cmdline_magic magic)
{
	size_t offset;

	if (!fastboot_get_cmdline_offset(fb, magic, &offset))
		return;

	fb_cmd->magic = magic;
	fb_cmd->version = 0;
	vb2_update_fastboot_cmdline_checksum(fb_cmd);

	fastboot_write_misc(fb, offset, fb_cmd, sizeof(struct vb2_fastboot_cmdline));
}

static void fastboot_cmd_cmdline_get(struct FastbootOps *fb, const char *arg,
				     enum vb2_fastboot_cmdline_magic magic)
{
	struct vb2_fastboot_cmdline fb_cmd;
	bool in_quote = false;
	char *param;

	if (!fastboot_read_misc_cmdline(fb, &fb_cmd, magic))
		return;

	param = fb_cmd.cmdline;
	for (int i = 0; i < fb_cmd.len; i++) {
		if (fb_cmd.cmdline[i] == '"') {
			in_quote = !in_quote;
			continue;
		}
		if (in_quote)
			continue;

		if (!isspace(fb_cmd.cmdline[i]))
			continue;

		fb_cmd.cmdline[i] = '\0';
		fastboot_info(fb, "%s", param);
		param = &fb_cmd.cmdline[i + 1];
	}

	fastboot_succeed(fb);
}

static void fastboot_cmd_cmdline_add(struct FastbootOps *fb, const char *arg,
				     enum vb2_fastboot_cmdline_magic magic)
{
	struct vb2_fastboot_cmdline fb_cmd;
	const int arg_len = strlen(arg);

	if (!fastboot_read_misc_cmdline(fb, &fb_cmd, magic))
		return;

	if (arg_len + fb_cmd.len + 1 >= sizeof(fb_cmd.cmdline)) {
		fastboot_fail(fb, "Not enough space in fastboot cmdline");
		return;
	}

	memcpy(&fb_cmd.cmdline[fb_cmd.len], arg, arg_len);
	fb_cmd.len += arg_len + 1;
	fb_cmd.cmdline[fb_cmd.len - 1] = ' ';

	fastboot_write_misc_cmdline(fb, &fb_cmd, magic);
}

static void fastboot_cmd_cmdline_del(struct FastbootOps *fb, const char *arg,
				     enum vb2_fastboot_cmdline_magic magic)
{
	struct vb2_fastboot_cmdline fb_cmd;
	const int arg_len = strlen(arg);
	bool in_quote = false;
	char *param;

	if (!fastboot_read_misc_cmdline(fb, &fb_cmd, magic))
		return;

	param = fb_cmd.cmdline;
	for (int i = 0; i < fb_cmd.len; i++) {
		if (fb_cmd.cmdline[i] == '"') {
			in_quote = !in_quote;
			continue;
		}
		if (in_quote)
			continue;

		if (!isspace(fb_cmd.cmdline[i]))
			continue;

		if (arg_len != fb_cmd.cmdline + i - param ||
		    strncmp(param, arg, arg_len)) {
			param = &fb_cmd.cmdline[i + 1];
			continue;
		}
		/* Found parameter to remove */
		break;
	}
	if (param >= fb_cmd.cmdline + fb_cmd.len) {
		fastboot_fail(fb, "Parameter not found");
		return;
	}

	memmove(param, param + arg_len + 1,
		fb_cmd.len - (param - fb_cmd.cmdline) - arg_len - 1);
	fb_cmd.len -= arg_len + 1;

	fastboot_write_misc_cmdline(fb, &fb_cmd, magic);
}

static void fastboot_cmd_cmdline_set(struct FastbootOps *fb, const char *arg,
				     enum vb2_fastboot_cmdline_magic magic)
{
	struct vb2_fastboot_cmdline fb_cmd;
	const int arg_len = strlen(arg);

	if (arg_len + 1 >= sizeof(fb_cmd.cmdline)) {
		fastboot_fail(fb, "Not enough space in fastboot cmdline");
		return;
	}

	if (arg_len > 0) {
		memcpy(fb_cmd.cmdline, arg, arg_len);
		fb_cmd.cmdline[arg_len] = ' ';
		fb_cmd.len = arg_len + 1;
	} else {
		fb_cmd.len = 0;
	}

	fastboot_write_misc_cmdline(fb, &fb_cmd, magic);
}

static void fastboot_cmd_oem_cmdline_get(struct FastbootOps *fb, const char *arg)
{
	fastboot_cmd_cmdline_get(fb, arg, VB2_FASTBOOT_CMDLINE_MAGIC);
}

static void fastboot_cmd_oem_cmdline_add(struct FastbootOps *fb, const char *arg)
{
	fastboot_cmd_cmdline_add(fb, arg, VB2_FASTBOOT_CMDLINE_MAGIC);
}

static void fastboot_cmd_oem_cmdline_del(struct FastbootOps *fb, const char *arg)
{
	fastboot_cmd_cmdline_del(fb, arg, VB2_FASTBOOT_CMDLINE_MAGIC);
}

static void fastboot_cmd_oem_cmdline_set(struct FastbootOps *fb, const char *arg)
{
	fastboot_cmd_cmdline_set(fb, arg, VB2_FASTBOOT_CMDLINE_MAGIC);
}

static void fastboot_cmd_oem_bootconfig_get(struct FastbootOps *fb, const char *arg)
{
	fastboot_cmd_cmdline_get(fb, arg, VB2_FASTBOOT_BOOTCONFIG_MAGIC);
}

static void fastboot_cmd_oem_bootconfig_add(struct FastbootOps *fb, const char *arg)
{
	fastboot_cmd_cmdline_add(fb, arg, VB2_FASTBOOT_BOOTCONFIG_MAGIC);
}

static void fastboot_cmd_oem_bootconfig_del(struct FastbootOps *fb, const char *arg)
{
	fastboot_cmd_cmdline_del(fb, arg, VB2_FASTBOOT_BOOTCONFIG_MAGIC);
}

static void fastboot_cmd_oem_bootconfig_set(struct FastbootOps *fb, const char *arg)
{
	fastboot_cmd_cmdline_set(fb, arg, VB2_FASTBOOT_BOOTCONFIG_MAGIC);
}

// `fastboot oem get-kernels` returns a list of slot letter:kernel mapping.
// This is useful for us because the partition tables are more flexible than on
// more traditional devices, so there could be many kernels.
// For instance, installing Fuchsia on a Chromebook will result in the device
// having five kernel partitions:
// KERN-A
// KERN-B
// zircon-a
// zircon-b
// zircon-r
//
// We map them to fastboot slots by having the first slot be the first partition
// we found.
struct get_kernels_ctx {
	struct FastbootOps *fb;
};
static bool get_kernels_cb(void *ctx, int index, GptEntry *e,
			   char *partition_name)
{
	char slot = get_slot_for_partition_name(e, partition_name);
	if (slot == 0)
		return false;

	struct get_kernels_ctx *gk = (struct get_kernels_ctx *)ctx;
	fastboot_info(gk->fb, "%c:%s:prio=%d", slot, partition_name,
		      GetEntryPriority(e));
	return false;
}
static void fastboot_cmd_oem_get_kernels(struct FastbootOps *fb, const char *arg)
{
	struct fastboot_disk disk;
	if (!fastboot_disk_init(&disk)) {
		fastboot_fail(fb, "Failed to init disk");
		return;
	}
	struct get_kernels_ctx ctx = {
		.fb = fb,
	};
	fastboot_disk_foreach_partition(&disk, get_kernels_cb, &ctx);
	fastboot_disk_destroy(&disk);
	fastboot_succeed(fb);
}

#define BCB_RECOVERY_ARG0 "recovery"
#define BCB_RECOVERY_ARG_FASTBOOT "--fastboot"

static void fastboot_cmd_reboot_to_recovery(struct FastbootOps *fb, const char *arg)
{
	struct vb2_bootloader_message bcb;

	memset(&bcb, 0, sizeof(bcb));
	strcpy(bcb.command, "boot-recovery");

	/* Setup recovery arguments depending on the target */
	if (!strcmp("fastboot", arg)) {
		snprintf(bcb.recovery, sizeof(bcb.recovery), "%s\n%s\n", BCB_RECOVERY_ARG0,
			 BCB_RECOVERY_ARG_FASTBOOT);
	} else if (!strcmp("recovery", arg)) {
		snprintf(bcb.recovery, sizeof(bcb.recovery), "%s\n", BCB_RECOVERY_ARG0);
	} else {
		fastboot_fail(fb, "Unknown reboot target");
		return;
	}

	fastboot_write_misc(fb, 0, &bcb, sizeof(bcb));

	/*
	 * TODO(b/370988331): We should force boot from internal drive without rebooting
	 *                    to speed up this.
	 */
	fb->state = REBOOT;
}

static void fastboot_cmd_reboot(struct FastbootOps *fb, const char *arg)
{
	fastboot_succeed(fb);
	fb->state = REBOOT;
}

static void fastboot_cmd_set_active(struct FastbootOps *fb, const char *arg)
{
	struct fastboot_disk disk;
	if (!fastboot_disk_init(&disk)) {
		fastboot_fail(fb, "Failed to init disk");
		return;
	}
	GptEntry *slot = fastboot_get_kernel_for_slot(&disk, arg[0]);
	if (slot == NULL) {
		fastboot_fail(fb, "Could not find slot");
		goto out;
	}

	fastboot_slots_disable_all(&disk);
	GptUpdateKernelWithEntry(disk.gpt, slot, GPT_UPDATE_ENTRY_ACTIVE);

	fastboot_succeed(fb);
out:
	fastboot_disk_destroy(&disk);
}
static void fastboot_cmd_oem_set_priority(struct FastbootOps *fb,
					  const char *arg)
{
	struct fastboot_disk disk;
	char *partition_name;
	int priority;
	char *priority_str;
	char *saveptr;

	if (!fastboot_disk_init(&disk)) {
		fastboot_fail(fb, "Failed to init disk");
		return;
	}

	partition_name = strtok_r((char *)arg, ":", &saveptr);
	if (!partition_name) {
		fastboot_fail(fb, "Missing partition name");
		goto out;
	}

	priority_str = strtok_r(NULL, ":", &saveptr);
	if (!priority_str) {
		fastboot_fail(fb, "Missing priority");
		goto out;
	}

	priority = strtol(priority_str, NULL, 10);
	if (priority < 0 || priority > 15) {

		fastboot_fail(fb, "Invalid priority %0d (valid range is 0-15)", priority);
		goto out;
	}

	GptEntry *e = fastboot_find_partition(&disk, partition_name);
	if (!e) {
		fastboot_fail(fb, "Could not find partition");
		goto out;
	}

	int old_priority = disk.gpt->current_priority;
	disk.gpt->current_priority = priority; // Set the priority in GptData
	if (GptUpdateKernelWithEntry(disk.gpt, e, GPT_UPDATE_ENTRY_SET_PRIORITY) == GPT_SUCCESS) {
		fastboot_succeed(fb);
	} else {
		disk.gpt->current_priority = old_priority; // Restore the old priority
		fastboot_fail(fb, "Failed to set priority");
	}

out:
	fastboot_disk_destroy(&disk);
}
static void fastboot_cmd_oem_set_successful(struct FastbootOps *fb,
					    const char *arg)
{
	struct fastboot_disk disk;
	const char *state_str = NULL;
	int state = -1;
	GptEntry *entry = NULL;

	const Guid guid_chromeos_kernel = GPT_ENT_TYPE_CHROMEOS_KERNEL;
	const Guid guid_android_vbmeta = GPT_ENT_TYPE_ANDROID_VBMETA;

	const int arg_len = strlen(arg);
	char *arg_copy = malloc(arg_len+1);
	if (!arg_copy) {
		fastboot_fail(fb, "memory allocation failed");
		return;
	}
	strcpy(arg_copy, arg);

	char *last_colon = strrchr(arg_copy, ':');

	/* Check if colon was found and is not the first or last character*/
	if (!last_colon || last_colon == arg_copy ||
	    last_colon == arg_copy + strlen(arg_copy) - 1) {
		fastboot_fail(fb, "Invalid arguments. Use: oem "
				  "set-successful:<partition>:<0|1>");
		free(arg_copy);
		return;
	}

	*last_colon = '\0'; /* Null-terminate arg_copy at the colon position */
	state_str = last_colon + 1; /* Points into arg_copy, after the new null*/
	/* Now arg_copy contains the partition name*/

	size_t state_len = strlen(state_str);

	/* Parse the state (0 or 1)*/
	if (state_len == 1 && state_str[0] == '0') {
		state = 0;
	} else if (state_len == 1 && state_str[0] == '1') {
		state = 1;
	} else {
		fastboot_fail(fb, "Invalid state value. Must be 0 or 1.");
		free(arg_copy);
		return;
	}

	if (!fastboot_disk_init(&disk)) {
		fastboot_fail(fb, "Failed to init disk");
		free(arg_copy);
		return;
	}

	entry = fastboot_find_partition(&disk, arg_copy);
	if (!entry) {
		fastboot_fail(fb, "Partition '%s' not found", arg_copy);
		goto cleanup;
	}

	/* Check if it's a bootable entry type*/
	if (memcmp(&entry->type, &guid_chromeos_kernel, sizeof(Guid)) != 0 &&
	    memcmp(&entry->type, &guid_android_vbmeta, sizeof(Guid)) != 0) {
		fastboot_fail(fb, "Partition '%s' is not a bootable entry type",
			      arg_copy); /* Use arg_copy for message*/
		goto cleanup;
	}

	disk.gpt->current_successful = state;

	int ret = GptUpdateKernelWithEntry(disk.gpt, entry,
					   GPT_UPDATE_ENTRY_SUCCESSFUL);
	if (ret != GPT_SUCCESS) {
		fastboot_fail(fb, "Failed to update GPT entry");
		goto cleanup;
	}

	if (WriteAndFreeGptData(disk.disk, disk.gpt)) {
		disk.gpt = NULL;
		fastboot_fail(fb, "Failed to write GPT data");
	} else {
		disk.gpt = NULL;
		fastboot_succeed(fb);
	}

cleanup:
	free(arg_copy);
	if (disk.gpt) {
		fastboot_disk_destroy(&disk);
	}
}

#define CMD_ARGS(_name, _sep, _fn)                                             \
	{                                                                      \
		.name = _name, .has_args = true, .sep = _sep, .fn = _fn        \
	}
#define CMD_NO_ARGS(_name, _fn)                                                \
	{                                                                      \
		.name = _name, .has_args = false, .fn = _fn                    \
	}
struct fastboot_cmd fastboot_cmds[] = {
	CMD_NO_ARGS("continue", fastboot_cmd_continue),
	CMD_ARGS("download", ':', fastboot_cmd_download),
	CMD_ARGS("erase", ':', fastboot_cmd_erase),
	CMD_ARGS("flash", ':', fastboot_cmd_flash),
	CMD_ARGS("getvar", ':', fastboot_cmd_getvar),
	CMD_ARGS("oem cmdline add", ' ', fastboot_cmd_oem_cmdline_add),
	CMD_ARGS("oem cmdline del", ' ', fastboot_cmd_oem_cmdline_del),
	CMD_ARGS("oem cmdline set", ' ', fastboot_cmd_oem_cmdline_set),
	CMD_NO_ARGS("oem cmdline", fastboot_cmd_oem_cmdline_get),
	CMD_ARGS("oem bootconfig add", ' ', fastboot_cmd_oem_bootconfig_add),
	CMD_ARGS("oem bootconfig del", ' ', fastboot_cmd_oem_bootconfig_del),
	CMD_ARGS("oem bootconfig set", ' ', fastboot_cmd_oem_bootconfig_set),
	CMD_NO_ARGS("oem bootconfig", fastboot_cmd_oem_bootconfig_get),
	CMD_NO_ARGS("oem get-kernels", fastboot_cmd_oem_get_kernels),
	CMD_ARGS("oem set-priority", ':', fastboot_cmd_oem_set_priority),
	CMD_ARGS("oem set-successful", ':', fastboot_cmd_oem_set_successful),
	CMD_ARGS("reboot", '-', fastboot_cmd_reboot_to_recovery),
	CMD_NO_ARGS("reboot", fastboot_cmd_reboot),
	CMD_ARGS("set_active", ':', fastboot_cmd_set_active),
	{
		.name = NULL,
	},
};
