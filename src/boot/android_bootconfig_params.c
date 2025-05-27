// SPDX-License-Identifier: GPL-2.0

#include <libpayload.h>
#include <vb2_android_bootimg.h>
#include <vb2_api.h>

#include "base/init_funcs.h"
#include "base/timestamp.h"
#include "base/vpd_util.h"
#include "boot/android_bootconfig_params.h"
#include "boot/bootconfig.h"
#include "boot/commandline.h"
#include "vboot/boot_policy.h"
#include "vboot/util/commonparams.h"

#define SERIAL_NUM_KEY_STR "androidboot.serialno"
#define MAX_SERIAL_NUM_LENGTH CB_MAX_SERIALNO_LENGTH

#define BOOTTIME_KEY_STR "androidboot.boottime"
/* 20 characters are sufficient for max uint64 18446744073709551615. Prepend with "firmware:" */
#define MAX_BOOTTIME_LENGTH 32

#define DISPLAY_ORIENTATION_KEY_STR "androidboot.surface_flinger.primary_display_orientation"
#define MAX_DISPLAY_ORIENTATION_LENGTH sizeof("ORIENTATION_xxx")

#define HWID_KEY_STR "androidboot.product.hardware.id"

#define SKU_ID_KEY_STR "androidboot.product.vendor.sku"
/* 20 characters for model name. Suffix with 12 characters for SKU ID */
#define MAX_SKU_ID_LENGTH 32

static int append_hwid(struct bootconfig *bc)
{
	char hwid[VB2_GBB_HWID_MAX_SIZE];
	uint32_t hwid_size = sizeof(hwid);

	if (vb2api_gbb_read_hwid(vboot_get_context(), hwid, &hwid_size)) {
		printf("No HWID in GBB\n");
		return -1;
	}
	return bootconfig_append(bc, HWID_KEY_STR, hwid);
}

static int append_serial_num(struct bootconfig *bc)
{
	char serial_num[MAX_SERIAL_NUM_LENGTH];

	if (!vpd_gets("serial_number", serial_num, sizeof(serial_num))) {
		printf("No serial number in vpd\n");
		return -1;
	}
	return bootconfig_append(bc, SERIAL_NUM_KEY_STR, serial_num);
}

static int append_display_orientation(struct bootconfig *bc)
{
	char orientation_map[][MAX_DISPLAY_ORIENTATION_LENGTH] = {
		[CB_FB_ORIENTATION_NORMAL] = "ORIENTATION_0",
		[CB_FB_ORIENTATION_BOTTOM_UP] = "ORIENTATION_180",
		[CB_FB_ORIENTATION_LEFT_UP] = "ORIENTATION_270",
		[CB_FB_ORIENTATION_RIGHT_UP] = "ORIENTATION_90",
	};
	uint8_t orientation = lib_sysinfo.framebuffer.orientation;

	if (orientation >= ARRAY_SIZE(orientation_map)) {
		printf("%s: Unexpected display orientation: %d\n", __func__, orientation);
		return -1;
	}
	return bootconfig_append(bc, DISPLAY_ORIENTATION_KEY_STR, orientation_map[orientation]);
}

static int append_skuid(struct bootconfig *bc)
{
	char sku_id_str[MAX_SKU_ID_LENGTH];
	uint32_t sku_id;
	struct cb_mainboard *mainboard =
		phys_to_virt(lib_sysinfo.cb_mainboard);
	const char *mb_part_string = cb_mb_part_string(mainboard);

	sku_id = lib_sysinfo.sku_id;
	int len = snprintf(sku_id_str, sizeof(sku_id_str), "%s_%u", mb_part_string, sku_id);
	if (len < 0 || len >= sizeof(sku_id_str))
		return -1;

	sku_id_str[0] = tolower(sku_id_str[0]);
	return bootconfig_append(bc, SKU_ID_KEY_STR, sku_id_str);
}

enum bootconfig_param_index {
	SERIAL_NUM,
	DISPLAY_ORIENTATION,
	HWID,
	SKU_ID,
};

static struct {
	const char *name;
	int (*const append)(struct bootconfig *bc);
	bool exists;
} params[] = {
	[SERIAL_NUM] = {
		.name = SERIAL_NUM_KEY_STR,
		.append = append_serial_num,
		.exists = false,
	},
	[DISPLAY_ORIENTATION] = {
		.name = DISPLAY_ORIENTATION_KEY_STR,
		.append = append_display_orientation,
		.exists = false,
	},
	[HWID] = {
		.name = HWID_KEY_STR,
		.append = append_hwid,
		.exists = false,
	},
	[SKU_ID] = {
		.name = SKU_ID_KEY_STR,
		.append = append_skuid,
		.exists = false,
	},
};

int append_android_bootconfig_params(struct bootconfig *bc)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(params); i++) {
		int ret = params[i].append(bc);
		if (ret < 0) {
			printf("Cannot append %s to Android bootconfig!\n", params[i].name);
			continue;
		}
		params[i].exists = true;
	}
	return 0;
}

int append_android_bootconfig_boottime(struct boot_info *bi)
{
	struct bootconfig bc;
	struct bootconfig_trailer *bc_trailer;

	if (!bi->ramdisk_size)
		return -1;

	bc_trailer = (struct bootconfig_trailer *)((uintptr_t)bi->ramdisk_addr +
		      bi->ramdisk_size - sizeof(*bc_trailer));

	if (bootconfig_reinit(&bc, bc_trailer))
		return -1;

	/* Append current boottime */
	uint64_t boot_time_ms = get_us_since_pre_cpu_reset() / USECS_PER_MSEC;
	char boottime[sizeof(BOOTCONFIG_MAX_BOOTTIME_STR)];
	snprintf(boottime, sizeof(boottime), "firmware:%"PRIu64, boot_time_ms);
	if (bootconfig_append(&bc, BOOTCONFIG_BOOTTIME_KEY_STR, boottime)) {
		printf("%s: Cannot append boottime", __func__);
		return -1;
	}
	/* Recalculate bootconfig checksum after changes */
	bootconfig_checksum_recalculate(&bc, bc_trailer);

	return 0;
}
