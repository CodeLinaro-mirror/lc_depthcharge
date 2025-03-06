// SPDX-License-Identifier: GPL-2.0

#include <libpayload.h>
#include <lvgl.h>

#include "vboot/ui.h"
#include "vboot/ui/lvgl/font.h"

vb2_error_t ui_lvgl_get_font(const char *font_name, const lv_font_t **font)
{
	static char font_name_cache[UI_ASSET_FILENAME_MAX_LEN + 1];
	static lv_font_t *font_cache;
	struct ui_asset font_asset;

	*font = NULL;
	if (strncmp(font_name, font_name_cache, sizeof(font_name_cache)) == 0) {
		*font = font_cache;
		return VB2_SUCCESS;
	}

	if (strlen(font_name) >= sizeof(font_name_cache)) {
		UI_ERROR("Font name %s too long\n", font_name);
		return VB2_ERROR_UI_DRAW_FAILURE;
	}

	/* Load font to cache */
	VB2_TRY(ui_load_font(font_name, &font_asset));
	if (font_cache)
		lv_binfont_destroy(font_cache);
	font_cache = lv_binfont_create_from_buffer((void *)font_asset.data, font_asset.size);
	strcpy(font_name_cache, font_name);

	*font = font_cache;
	return VB2_SUCCESS;
}
