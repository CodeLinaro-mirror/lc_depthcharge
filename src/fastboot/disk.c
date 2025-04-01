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

#include <commonlib/list.h>
#include <stdbool.h>
#include <stdio.h>

#include "base/gpt.h"
#include "ctype.h"
#include "drivers/storage/blockdev.h"
#include "fastboot/disk.h"
#include "fastboot/fastboot.h"
#include "fastboot/sparse.h"
#include "gpt.h"
#include "gpt_misc.h"
#include <libpayload.h>
#include <string.h>

bool fastboot_disk_init(struct fastboot_disk *disk)
{
	memset(disk, 0, sizeof(*disk));
	struct list_node *devs;
	uint32_t count = get_all_bdevs(BLOCKDEV_FIXED, &devs);

	if (count != 1) {
		FB_DEBUG("wrong number of fixed disks (found %d, wanted 1)\n",
			 count);
		return false;
	}

	BlockDev *bdev = NULL;
	list_for_each(bdev, *devs, list_node)
	{
		disk->disk = bdev;
		break;
	}

	if (disk->disk == NULL) {
		FB_DEBUG("No disk found\n");
		return false;
	}

	FB_DEBUG("Using disk '%s'\n", disk->disk->name);

	disk->gpt = alloc_gpt(disk->disk);
	if (disk->gpt == NULL) {
		return false;
	}
	return true;
}

void fastboot_disk_destroy(struct fastboot_disk *disk)
{
	free_gpt(disk->disk, disk->gpt);
}

char *fastboot_get_entry_name(GptEntry *e)
{
	if (IsUnusedEntry(e))
		return NULL;

	return utf16le_to_ascii(e->name, ARRAY_SIZE(e->name));
}

bool fastboot_disk_foreach_partition(struct fastboot_disk *disk,
				     disk_foreach_callback_t cb, void *ctx)
{
	GptHeader *h = (GptHeader *)disk->gpt->primary_header;
	GptEntry *e;
	bool stop = false;
	for (int i = 0; !stop && i < h->number_of_entries; i++) {
		e = (GptEntry *)&disk->gpt
			    ->primary_entries[i * h->size_of_entry];
		char *name = fastboot_get_entry_name(e);
		if (name == NULL)
			continue;
		stop = cb(ctx, i, e, name);
		free(name);
	}
	return stop;
}

struct find_partition_ctx {
	const char *name;
	GptEntry *result;
};

static bool find_partition_callback(void *ctx, int index, GptEntry *e,
				    char *partition_name)
{
	struct find_partition_ctx *fpctx = (struct find_partition_ctx *)ctx;

	if (!strcmp(fpctx->name, partition_name)) {
		fpctx->result = e;
		return true;
	}

	return false;
}
GptEntry *fastboot_find_partition(struct fastboot_disk *disk, const char *partition_name)
{
	struct find_partition_ctx fp = {.name = partition_name,
					.result = NULL};
	fastboot_disk_foreach_partition(disk, find_partition_callback, &fp);
	return fp.result;
}

int fastboot_get_number_of_partitions(struct fastboot_disk *disk)
{
	GptHeader *h = (GptHeader *)disk->gpt->primary_header;

	return h->number_of_entries;
}

GptEntry *fastboot_get_partition(struct fastboot_disk *disk, unsigned int index)
{
	GptHeader *h = (GptHeader *)disk->gpt->primary_header;

	if (index >= h->number_of_entries)
		return NULL;

	return (GptEntry *)&disk->gpt->primary_entries[index * h->size_of_entry];
}

bool fastboot_read(struct fastboot_disk *disk, const char *partition_name, void *data,
		   size_t data_len, size_t offset)
{
	if (offset % disk->disk->block_size) {
		FB_DEBUG("Offset %zu not block size aligned %u\n", offset,
			 disk->disk->block_size);
		return false;
	}

	if (data_len % disk->disk->block_size) {
		FB_DEBUG("Buffer size %zu not block size aligned %u\n", data_len,
			 disk->disk->block_size);
		return false;
	}

	GptEntry *e = fastboot_find_partition(disk, partition_name);
	if (!e) {
		FB_DEBUG("Could not find partition\n");
		return false;
	}

	uint64_t space = GptGetEntrySizeLba(e);
	uint64_t data_blocks = data_len / disk->disk->block_size;
	uint64_t data_blocks_offset = offset / disk->disk->block_size;
	if (data_blocks_offset + data_blocks > space) {
		FB_DEBUG("Too large read (last data block %llu, space %llu)\n",
			 data_blocks + data_blocks_offset, space);
		return false;
	}

	FB_DEBUG("Reading LBA %llu to %llu, num blocks = %llu, data "
		 "len = %zu, block size = %u\n",
		 e->starting_lba + data_blocks_offset,
		 e->starting_lba + data_blocks + data_blocks_offset,
		 data_blocks, data_len, disk->disk->block_size);
	lba_t blocks_read = disk->disk->ops.read(&disk->disk->ops,
						 e->starting_lba + data_blocks_offset,
						 data_blocks, data);
	if (blocks_read != data_blocks) {
		FB_DEBUG("Read %llu blocks instead of %llu\n", blocks_read, data_blocks);
		return false;
	}

	return true;
}

void fastboot_write(struct FastbootOps *fb, struct fastboot_disk *disk,
		    const char *partition_name, void *data, size_t data_len, size_t offset)
{
	GptEntry *e = fastboot_find_partition(disk, partition_name);
	if (!e) {
		fastboot_fail(fb, "Could not find partition");
		return;
	}

	if (is_sparse_image(data)) {
		if (offset) {
			FB_FAIL_AND_DEBUG(fb, "Non-zero offset for sparse image write");
			return;
		}

		FB_DEBUG("Writing sparse image to LBA %llu to %llu\n",
			 e->starting_lba, e->ending_lba);
		if (!write_sparse_image(fb, disk, e, data, data_len))
			fastboot_succeed(fb);

		return;
	}

	if (offset % disk->disk->block_size) {
		fastboot_fail(fb, "Offset %zu not block size aligned %u\n", offset,
			      disk->disk->block_size);
		return;
	}

	if (data_len % disk->disk->block_size) {
		fastboot_fail(fb, "Buffer size %zu not block size aligned %u\n", data_len,
			      disk->disk->block_size);
		return;
	}

	uint64_t space = GptGetEntrySizeLba(e);
	uint64_t data_blocks = data_len / disk->disk->block_size;
	uint64_t data_blocks_offset = offset / disk->disk->block_size;
	if (data_blocks + data_blocks_offset > space) {
		fastboot_fail(fb, "Image is too big");
		return;
	}

	FB_DEBUG("Writing LBA %llu to %llu, num blocks = %llu, data "
		 "len = %zu, block size = %u\n",
		 e->starting_lba + data_blocks_offset,
		 e->starting_lba + data_blocks_offset + data_blocks,
		 data_blocks, data_len, disk->disk->block_size);
	lba_t blocks_written = disk->disk->ops.write(
		&disk->disk->ops, e->starting_lba + data_blocks_offset, data_blocks, data);
	if (blocks_written != data_blocks) {
		fastboot_fail(fb, "Failed to write");
		return;
	}

	fastboot_succeed(fb);
	return;
}

void fastboot_erase(struct FastbootOps *fb, struct fastboot_disk *disk,
		    const char *partition_name)
{
	GptEntry *e = fastboot_find_partition(disk, partition_name);
	if (!e) {
		fastboot_fail(fb, "Could not find partition");
		return;
	}

	lba_t space = GptGetEntrySizeLba(e);
	if ((disk->disk->ops.erase == NULL) ||
	    disk->disk->ops.erase(&disk->disk->ops, e->starting_lba, space)) {
		if (blockdev_fill_write(&disk->disk->ops, e->starting_lba, space,
					0xffffffff) != space) {
			fastboot_fail(fb, "Failed to erase");
			return;
		}
	}
	fastboot_succeed(fb);
}

/************************* SLOT LOGIC ******************************/

// Returns 0 if the partition is not valid.
char get_slot_for_partition_name(GptEntry *e, char *partition_name)
{
	const Guid cros_kernel_guid = GPT_ENT_TYPE_CHROMEOS_KERNEL;
	if (memcmp(&e->type, &cros_kernel_guid, sizeof(cros_kernel_guid)) != 0)
		return 0;
	int len = strlen(partition_name);
	// expect partition names ending with -X or _x
	if (partition_name[len - 2] != '-' && partition_name[len - 2] != '_') {
		FB_DEBUG("ignoring kernel name '%s' - missing suffix\n",
			 partition_name);
		return 0;
	}

	if (!isalpha(partition_name[len - 1])) {
		FB_DEBUG("ignoring kernel name '%s' - slot is not a letter\n",
			 partition_name);
		return 0;
	}

	// make all slots lowercase.
	char slot = partition_name[len - 1];
	if (slot < 'a')
		slot += ('a' - 'A');
	return slot;
}

struct kpi_ctx {
	int kernel_count;
	// bitmap of present slots.
	// bit 0 = slot a
	// bit 1 = slot b
	// ...
	// bit 26 = slot z
	uint32_t present;
};
static bool check_kernel_partition_info(void *ctx, int index, GptEntry *e,
					char *partition_name)
{
	struct kpi_ctx *info = (struct kpi_ctx *)ctx;
	char slot = get_slot_for_partition_name(e, partition_name);
	if (slot == 0)
		return false;

	FB_DEBUG("kernel name '%s' => slot %c or %d\n", partition_name, slot,
		 slot - 'a');
	uint32_t bit = 1 << (slot - 'a');
	if (info->present & bit) {
		FB_DEBUG("Multiple slots for '%c'!\n", slot);
	}
	info->present |= bit;
	info->kernel_count++;

	return false;
}
int fastboot_get_slot_count(struct fastboot_disk *disk)
{
	struct kpi_ctx result = {
		.kernel_count = 0,
	};
	fastboot_disk_foreach_partition(disk, check_kernel_partition_info,
					&result);
	return result.kernel_count;
}

struct find_slot_ctx {
	GptEntry *target_entry;
	char desired_slot;
};
static bool find_slot_callback(void *ctx, int index, GptEntry *e,
			       char *partition_name)
{
	char slot = get_slot_for_partition_name(e, partition_name);
	if (slot == 0)
		return false;

	struct find_slot_ctx *fs = (struct find_slot_ctx *)ctx;
	if (slot == fs->desired_slot) {
		fs->target_entry = e;
		return true;
	}

	return false;
}

GptEntry *fastboot_get_kernel_for_slot(struct fastboot_disk *disk, char slot)
{

	if (slot < 'a') {
		return NULL;
	}
	struct find_slot_ctx ctx = {
		.target_entry = NULL,
		.desired_slot = slot,
	};
	if (fastboot_disk_foreach_partition(disk, find_slot_callback, &ctx)) {
		return ctx.target_entry;
	}
	return NULL;
}

static bool disable_all_callback(void *ctx, int index, GptEntry *e,
				 char *partition_name)
{
	if (get_slot_for_partition_name(e, partition_name) == 0)
		return false;

	GptUpdateKernelWithEntry((GptData *)ctx, e, GPT_UPDATE_ENTRY_INVALID);
	return false;
}
void fastboot_slots_disable_all(struct fastboot_disk *disk)
{
	fastboot_disk_foreach_partition(disk, disable_all_callback, disk->gpt);
}

struct has_slot_ctx {
	const char *name;
	int len;
	bool partition_found;
	bool has_slot;
};

static bool has_slot_callback(void *ctx, int index, GptEntry *e,
			      char *partition_name)
{
	struct has_slot_ctx *hctx = (struct has_slot_ctx *)ctx;
	size_t partition_name_len = strlen(partition_name);

	bool has_slot = partition_name_len > 2 &&
		(partition_name[partition_name_len - 2] == '-' ||
                partition_name[partition_name_len - 2] == '_') &&
                isalpha(partition_name[partition_name_len - 1]);

	if ((has_slot && (partition_name_len != hctx->len + 2)) ||
		(!has_slot && (partition_name_len != hctx->len)))
		return false; // wrong length

	if(strncmp(partition_name, hctx->name, hctx->len) == 0) {
		FB_DEBUG("%s, %s: names match!", partition_name, hctx->name);
		hctx->partition_found = true;
		hctx->has_slot = has_slot;
		return true; // Stop iteration
	}
	return false;
}

bool fastboot_has_slot(struct fastboot_disk *disk, const char *name, int len,
		      bool *partition_found)
{
	struct has_slot_ctx ctx = {
		.name = name,
		.len = len,
		.partition_found = false,
		.has_slot = false,
	};
	fastboot_disk_foreach_partition(disk, has_slot_callback, &ctx);
	*partition_found = ctx.partition_found;
	if (!ctx.partition_found)
		FB_DEBUG("Could not find a partition named %s\n", name);
	return ctx.has_slot;
}

struct slot_suffixes_ctx {
	char *suffixes;
	size_t len;
	size_t max_len;
};

static bool slot_suffixes_callback(void *ctx, int index, GptEntry *e,
				   char *partition_name)
{
	struct slot_suffixes_ctx *ss = (struct slot_suffixes_ctx *)ctx;
	char slot = get_slot_for_partition_name(e, partition_name);
	if (slot == 0)
		return false;
	if (ss->len > 0) {
		if (ss->len + 2 >= ss->max_len) {
			FB_DEBUG("Error: exceeded allocated space for suffixes (%0d) \n",
				FASTBOOT_MAX_SLOTS*2);
			return true;
		}
		ss->suffixes[ss->len++] = ',';
	}
	ss->suffixes[ss->len++] = slot;
	return false;
}

char *fastboot_get_slot_suffixes(struct fastboot_disk *disk)
{
	struct slot_suffixes_ctx ctx = {
		.suffixes = xzalloc(FASTBOOT_MAX_SLOTS*2),
		.max_len = FASTBOOT_MAX_SLOTS*2,
	};
	fastboot_disk_foreach_partition(disk, slot_suffixes_callback, &ctx);
	return ctx.suffixes;
}
