// SPDX-License-Identifier: GPL-2.0

#include <libpayload.h>
#include <pci.h>
#include <pci/pci.h>

#include "board/brya/include/variant.h"
#include "drivers/gpio/alderlake.h"
#include "drivers/soc/alderlake.h"
#include "drivers/storage/sdhci.h"
#include "drivers/storage/storage_common.h"
#include "vboot/util/commonparams.h"

#define EMMC_CLOCK_MIN		400000
#define EMMC_CLOCK_MAX		200000000

#define EC_PCH_INT_ODL		GPD2

const struct audio_config *variant_probe_audio_config(void)
{
	static struct audio_config config;
	struct vb2_context *ctx = vboot_get_context();

	config = (struct audio_config){
		.bus = {
		.type = AUDIO_PWM,
		.pwm.pad = GPP_D2,
		},
	};

	if (vb2api_gbb_get_flags(ctx) & VB2_GBB_FLAG_RUNNING_FAFT)
		return NULL;
	else
		return &config;
}

static const struct storage_config storage_configs[] = {
	{
		.media = STORAGE_EMMC,
		.pci_dev = PCH_DEV_EMMC,
		.emmc = {
			.platform_flags = SDHCI_PLATFORM_SUPPORTS_HS400ES,
			.clock_min = EMMC_CLOCK_MIN,
			.clock_max = EMMC_CLOCK_MAX
			},
	},
};

const struct storage_config *variant_get_storage_configs(size_t *count)
{
	*count = ARRAY_SIZE(storage_configs);
	return storage_configs;
}

const struct tpm_config *variant_get_tpm_config(void)
{
	static const struct tpm_config config = {
		.pci_dev = PCI_DEV(0, 0x15, 0),
	};

	return &config;
}

const int variant_get_ec_int(void)
{
	return EC_PCH_INT_ODL;
}
