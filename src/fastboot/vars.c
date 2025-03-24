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

#include <arch/virtual.h>
#include <ctype.h>
#include <string.h>
#include <sysinfo.h>

#include "base/gpt.h"
#include "fastboot/disk.h"
#include "fastboot/fastboot.h"
#include "fastboot/vars.h"
#include "base/vpd_util.h"
#include "vboot/firmware_id.h"

#define VAR_ARGS(_name, _sep, _var)                                            \
	{                                                                      \
		.name = _name, .has_args = true, .sep = _sep, .var = _var      \
	}
#define VAR_NO_ARGS(_name, _var)                                               \
	{                                                                      \
		.name = _name, .has_args = false, .var = _var                  \
	}
static fastboot_getvar_info_t fastboot_vars[] = {
	VAR_NO_ARGS("current-slot", VAR_CURRENT_SLOT),
	VAR_NO_ARGS("Disk-block-count", VAR_DISK_BLOCK_COUNT),
	VAR_NO_ARGS("Disk-block-size", VAR_DISK_BLOCK_SIZE),
	VAR_NO_ARGS("max-download-size", VAR_DOWNLOAD_SIZE),
	VAR_NO_ARGS("is-userspace", VAR_IS_USERSPACE),
	VAR_ARGS("partition-size", ':', VAR_PARTITION_SIZE),
	VAR_ARGS("partition-type", ':', VAR_PARTITION_TYPE),
	VAR_NO_ARGS("product", VAR_PRODUCT),
	VAR_NO_ARGS("secure", VAR_SECURE),
	VAR_NO_ARGS("slot-count", VAR_SLOT_COUNT),
	VAR_NO_ARGS("version", VAR_VERSION),
	VAR_NO_ARGS("version-bootloader", VAR_VERSION_BOOTLOADER),
	VAR_ARGS("has-slot", ':', VAR_HAS_SLOT),
	VAR_NO_ARGS("slot-suffixes", VAR_SLOT_SUFFIXES),
	VAR_ARGS("slot-successful", ':', VAR_SLOT_SUCCESSFUL),
	VAR_ARGS("slot-retry-count", ':', VAR_SLOT_RETRY_COUNT),
	VAR_ARGS("slot-unbootable", ':', VAR_SLOT_UNBOOTABLE),
	VAR_NO_ARGS("logical-block-size", VAR_LOGICAL_BLOCK_SIZE),
	/* erase-block-size is the same as logical-block-size, added for completeness*/
	VAR_NO_ARGS("erase-block-size", VAR_LOGICAL_BLOCK_SIZE),
	VAR_NO_ARGS("serialno", VAR_SERIALNO),
	{.name = NULL},
};

static void fastboot_getvar_all(struct FastbootOps *fb)
{
	char var_buf[FASTBOOT_MSG_MAX];

	for (int i = 0; fastboot_vars[i].name != NULL; i++) {
		fastboot_getvar_info_t *var = &fastboot_vars[i];

		if (var->has_args) {
			fastboot_getvar_result_t state;
			int arg = 0;

			do {
				size_t len = FASTBOOT_MSG_MAX;

				state = fastboot_getvar(fb, var->var, NULL, arg++,
							var_buf, &len);
				if (state == STATE_OK)
					fastboot_info(fb, "%s:%.*s", var->name, (int)len,
						      var_buf);
			} while (state == STATE_OK || state == STATE_TRY_NEXT);
		} else {
			size_t len = FASTBOOT_MSG_MAX;
			if (fastboot_getvar(fb, var->var, NULL, 0, var_buf, &len) == STATE_OK)
				fastboot_info(fb, "%s:%.*s", var->name, (int)len, var_buf);
		}
	}
	fastboot_succeed(fb);
}

void fastboot_cmd_getvar(struct FastbootOps *fb, const char *args)
{
	char var_buf[FASTBOOT_MSG_MAX];

	if (!strcmp(args, "all")) {
		fastboot_getvar_all(fb);
		return;
	}
	for (int i = 0; fastboot_vars[i].name != NULL; i++) {
		fastboot_getvar_info_t *var = &fastboot_vars[i];
		int name_len = strlen(var->name);

		if (strncmp(var->name, args, name_len))
			continue;

		if (var->has_args) {
			if (args[name_len] != var->sep)
				continue;
			args++;
		} else if (args[name_len] != '\0')
			continue;
		args += name_len;

		size_t var_len = FASTBOOT_MSG_MAX;
		fastboot_getvar_result_t state = fastboot_getvar(
			fb, var->var, args, 0, var_buf, &var_len);
		if (state == STATE_OK) {
			fastboot_okay(fb, "%.*s", (int)var_len, var_buf);
		} else {
			fastboot_fail(fb, "getvar failed - internal error");
		}
		return;
	}

	fastboot_fail(fb, "Unknown variable");
}

/* Helper function to get partition type string based on name */
static const char *get_partition_type_string(const char *name)
{
	if (!strcmp(name, "OEM")) {
		return "ext4";
	} else if (!strcmp(name, "EFI-SYSTEM")) {
		return "vfat";
	} else if (!strcmp(name, "metadata")) {
		return "ext4";
	} else if (!strcmp(name, "userdata")) {
		return "ext4";
	}
	return "raw"; /* All other partitions are "raw" type*/
}

static fastboot_getvar_result_t fastboot_get_partition_name_by_index(
					GptData *gpt,
					size_t index, char **name, GptEntry **part)
{
	if (gpt_get_number_of_partitions(gpt) <= index)
		return STATE_LAST;

	*part = gpt_get_partition(gpt, index);
	if (*part == NULL)
		return STATE_TRY_NEXT;

	*name = gpt_get_entry_name(*part);
	if (*name == NULL)
		return STATE_TRY_NEXT;

	return STATE_OK;
}

/* Iterates through GPT entries to find the 'index'-th kernel partition. */
static fastboot_getvar_result_t fastboot_get_kernel_slot_by_index(
	struct fastboot_disk *disk, size_t index, GptEntry **out_entry,
	char *out_slot_char)
{
	GptHeader *h = (GptHeader *)disk->gpt->primary_header;
	int kernel_slot_idx_counter = 0;

	for (int i = 0; i < h->number_of_entries; i++) {
		GptEntry *current_entry =
			(GptEntry *)&disk->gpt->primary_entries[i * h->size_of_entry];

		if (IsUnusedEntry(current_entry))
			continue;
		char *name = fastboot_get_entry_name(current_entry);
		if (name == NULL)
			continue;
		char slot_char = get_slot_for_partition_name(current_entry, name);
		free(name);
		if (slot_char == 0)
			continue;
		if (kernel_slot_idx_counter == index) {
			*out_entry = current_entry;
			*out_slot_char = slot_char;
			return STATE_OK; /* Found the 'index'-th kernel slot */
		}
		kernel_slot_idx_counter++;
	}
	return STATE_LAST;
}

static bool is_entry_unbootable(GptEntry *entry)
{
	return !IsBootableEntry(entry) || GetEntryPriority(entry) == 0 ||
		(!GetEntrySuccessful(entry) &&  GetEntryTries(entry) == 0);
}

fastboot_getvar_result_t fastboot_getvar(struct FastbootOps *fb, fastboot_var_t var,
					 const char *arg, size_t index, char *outbuf,
					 size_t *outbuf_len)
{
	GptEntry *part = NULL;
	size_t used_len = 0;
	char *name;

	switch (var) {
	case VAR_CURRENT_SLOT: {
		if (fastboot_disk_gpt_init(fb))
			return STATE_DISK_ERROR;

		/* Make sure that GptNextKernelEntry starts with fresh state */
		if (GptInit(fb->gpt) != GPT_SUCCESS)
			return STATE_DISK_ERROR;

		part = GptNextKernelEntry(fb->gpt);
		if (part == NULL)
			return STATE_DISK_ERROR;

		name = gpt_get_entry_name(part);
		if (name == NULL || name[0] == '\0')
			return STATE_DISK_ERROR;
		/* Get last character */
		while (name[1] != '\0')
			name++;

		used_len = snprintf(outbuf, *outbuf_len, "%s", name);
		free(name);
		break;
	}
	case VAR_DISK_BLOCK_COUNT:
		if (!fastboot_disk_init(&disk))
			return STATE_DISK_ERROR;
		used_len += snprintf(outbuf, *outbuf_len, "0x%llx", disk.disk->block_count);
		fastboot_disk_destroy(&disk);
		break;
	case VAR_DISK_BLOCK_SIZE:
		if (!fastboot_disk_init(&disk))
			return STATE_DISK_ERROR;
		used_len += snprintf(outbuf, *outbuf_len, "0x%x", disk.disk->block_size);
		fastboot_disk_destroy(&disk);
		break;
	case VAR_DOWNLOAD_SIZE:
		used_len = snprintf(outbuf, *outbuf_len, "0x%llx", FASTBOOT_MAX_DOWNLOAD_SIZE);
		break;
	case VAR_IS_USERSPACE:
		used_len = snprintf(outbuf, *outbuf_len, "no");
		break;
	case VAR_PARTITION_SIZE:
		if (fastboot_disk_gpt_init(fb))
			return STATE_DISK_ERROR;
		if (arg != NULL) {
			part = gpt_find_partition(fb->gpt, arg);
			if (part == NULL)
				return STATE_UNKNOWN_VAR;
		} else {
			fastboot_getvar_result_t state =
				fastboot_get_partition_name_by_index(fb->gpt, index, &name,
								     &part);
			if (state != STATE_OK)
				return state;
			used_len = snprintf(outbuf, *outbuf_len, "%s:", name);
			outbuf += used_len;
			*outbuf_len -= used_len;
			free(name);
		}

		used_len += snprintf(outbuf, *outbuf_len, "0x%llx",
				     GptGetEntrySizeBytes(fb->gpt, part));
		break;
	case VAR_PRODUCT: {
		struct cb_mainboard *mainboard =
			phys_to_virt(lib_sysinfo.cb_mainboard);
		const char *mb_part_string = cb_mb_part_string(mainboard);
		used_len = snprintf(outbuf, *outbuf_len, "%s", mb_part_string);
		break;
	}
	case VAR_SLOT_COUNT:
		if (fastboot_disk_gpt_init(fb))
			return STATE_DISK_ERROR;
		used_len = snprintf(outbuf, *outbuf_len, "%d",
				    fastboot_get_slot_count(fb->gpt));
		break;
	case VAR_SECURE:
		used_len = snprintf(outbuf, *outbuf_len, "no");
		break;
	case VAR_VERSION:
		used_len = snprintf(outbuf, *outbuf_len, "0.4");
		break;
	case VAR_VERSION_BOOTLOADER: {
		const char *fw_id = get_active_fw_id();
		if (fw_id == NULL)
			used_len = snprintf(outbuf, *outbuf_len, "unknown");
		else
			used_len = snprintf(outbuf, *outbuf_len, "%s", fw_id);
		break;
	}
	case VAR_HAS_SLOT: {
		bool has_slot = false;
		if (fastboot_disk_gpt_init(fb))
			return STATE_DISK_ERROR;
		if (arg != NULL) {
			bool partition_found = false;
			has_slot = fastboot_has_slot(fb->gpt, arg, strlen(arg),
						     &partition_found);
			if (!partition_found)
				return STATE_UNKNOWN_VAR;
		} else {
			int name_len;
			fastboot_getvar_result_t state =
				fastboot_get_partition_name_by_index(fb->gpt, index, &name,
								     &part);
			if (state != STATE_OK)
				return state;
			name_len = strlen(name);
			has_slot = partition_has_suffix(name);
			if (has_slot) {
				if (tolower(name[name_len - 1]) != 'a')
					return STATE_TRY_NEXT;
				name_len -= 2;
			}
			used_len = snprintf(outbuf, *outbuf_len, "%.*s:", name_len, name);
			outbuf += used_len;
			*outbuf_len -= used_len;
			free(name);
		}
		used_len += snprintf(outbuf, *outbuf_len, has_slot ? "yes" : "no");
		break;
	}
	case VAR_SLOT_SUFFIXES: {
		if (!fastboot_disk_init(&disk))
			return STATE_DISK_ERROR;
		char *suffixes = fastboot_get_slot_suffixes(&disk);
		if (suffixes == NULL) {
			fastboot_disk_destroy(&disk);
			return STATE_DISK_ERROR;
		}
		used_len = snprintf(outbuf, *outbuf_len, "%s", suffixes);
		free(suffixes);
		fastboot_disk_destroy(&disk);
		break;
	}
	case VAR_SLOT_SUCCESSFUL: {
		if (!fastboot_disk_init(&disk))
			return STATE_DISK_ERROR;

		if (arg != NULL) {
			if (strlen(arg) != 1 || !isalpha(arg[0])) {
				fastboot_disk_destroy(&disk);
				return STATE_UNKNOWN_VAR;
			}
			char slot_char_arg = tolower(arg[0]);
			GptEntry *e = fastboot_get_kernel_for_slot(&disk, slot_char_arg);
			if (e == NULL) {
				fastboot_disk_destroy(&disk);
				return STATE_UNKNOWN_VAR;
			}
			used_len = snprintf(outbuf, *outbuf_len, "%s",
					    GetEntrySuccessful(e) ? "yes" : "no");
		} else {
			/* Handling for "getvar all" - arg is NULL, use index. */
			GptEntry *entry_for_index = NULL;
			char slot_char_for_index = 0;
			fastboot_getvar_result_t find_slot_state =
				fastboot_get_kernel_slot_by_index(&disk, index, &entry_for_index,
					&slot_char_for_index);
			if (find_slot_state != STATE_OK) {
				fastboot_disk_destroy(&disk);
				return find_slot_state;
			}
			used_len = snprintf(outbuf, *outbuf_len, "%c:%s", slot_char_for_index,
					    GetEntrySuccessful(entry_for_index) ? "yes" : "no");
		}
		fastboot_disk_destroy(&disk);
		break;
	}
	case VAR_SLOT_RETRY_COUNT: {
		if (!fastboot_disk_init(&disk))
			return STATE_DISK_ERROR;

		if (arg != NULL) {
			if (strlen(arg) != 1 || !isalpha(arg[0])) {
				fastboot_disk_destroy(&disk);
				return STATE_UNKNOWN_VAR; // Invalid slot format
			}
			char slot_char_arg = tolower(arg[0]);
			GptEntry *e = fastboot_get_kernel_for_slot(&disk, slot_char_arg);
			if (e == NULL) {
				fastboot_disk_destroy(&disk);
				return STATE_UNKNOWN_VAR;
			}
			used_len = snprintf(outbuf, *outbuf_len, "%d", GetEntryTries(e));
		} else {
			// Handling for "getvar all"; arg is NULL, use index.
			GptEntry *entry_for_index = NULL;
			char slot_char_for_index = 0;
			fastboot_getvar_result_t find_slot_state =
				fastboot_get_kernel_slot_by_index(&disk, index, &entry_for_index,
								  &slot_char_for_index);
			if (find_slot_state != STATE_OK) {
				fastboot_disk_destroy(&disk);
				return find_slot_state;
			}
			used_len = snprintf(outbuf, *outbuf_len, "%c:%d", slot_char_for_index,
					    GetEntryTries(entry_for_index));
		}
		fastboot_disk_destroy(&disk);
		break;
	}
	case VAR_SLOT_UNBOOTABLE: {
		if (!fastboot_disk_init(&disk))
			return STATE_DISK_ERROR;

		if (arg != NULL) {
			if (strlen(arg) != 1 || !isalpha(arg[0])) {
				fastboot_disk_destroy(&disk);
				return STATE_UNKNOWN_VAR;
			}
			char slot_char_arg = tolower(arg[0]);
			GptEntry *e = fastboot_get_kernel_for_slot(
				&disk, slot_char_arg);
			if (e == NULL) {
				fastboot_disk_destroy(&disk);
				return STATE_UNKNOWN_VAR;
			}
			bool is_unbootable = is_entry_unbootable(e);
			used_len = snprintf(outbuf, *outbuf_len,
					    is_unbootable ? "yes" : "no");
		} else {
			/* Handling for "getvar all" - arg is NULL, use index. */
			GptEntry *entry_for_index = NULL;
			char slot_char_for_index = 0;
			fastboot_getvar_result_t find_slot_state =
				fastboot_get_kernel_slot_by_index(&disk, index,
					&entry_for_index, &slot_char_for_index);
			if (find_slot_state != STATE_OK) {
				fastboot_disk_destroy(&disk);
				return find_slot_state;
			}
			bool is_unbootable = is_entry_unbootable(entry_for_index);
			used_len = snprintf(outbuf, *outbuf_len, "%c:%s", slot_char_for_index,
					    is_unbootable ? "yes" : "no");
		}
		fastboot_disk_destroy(&disk);
		break;
	}
	case VAR_LOGICAL_BLOCK_SIZE:
		if (!fastboot_disk_init(&disk))
			return STATE_DISK_ERROR;

		if (!disk.disk) {
			fastboot_disk_destroy(&disk);
			return STATE_DISK_ERROR;
		}

		used_len = snprintf(outbuf, *outbuf_len, "0x%x", disk.disk->block_size);

		fastboot_disk_destroy(&disk);
		break;

	case VAR_SERIALNO: {
		u32 vpd_size;
		const void *vpd_data = vpd_find("serial_number", NULL, NULL, &vpd_size);

		if (vpd_data && vpd_size > 0)
			used_len = snprintf(outbuf, *outbuf_len, "%.*s",
					       (int)vpd_size, (const char *)vpd_data);
		else
			used_len = snprintf(outbuf, *outbuf_len, "unknown");
		break;
	}
	default:
		return STATE_UNKNOWN_VAR;
	}

	*outbuf_len = used_len;
	return STATE_OK;
}
