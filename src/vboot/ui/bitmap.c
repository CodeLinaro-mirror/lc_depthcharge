// SPDX-License-Identifier: GPL-2.0

#include <libpayload.h>
#include <string.h>
#include <vb2_api.h>

#include "vboot/ui.h"

vb2_error_t ui_get_language_name_bitmap(const char *locale_code,
					struct ui_asset *bitmap)
{
	return ui_get_language_name_asset(locale_code, "bmp", bitmap);
}

vb2_error_t ui_get_char_bitmap(const char c, struct ui_asset *bitmap)
{
	char file[UI_ASSET_FILENAME_MAX_LEN + 1];
	const char pattern[] = "idx%03d_%02x.bmp";

	snprintf(file, sizeof(file), pattern, c, c);
	return ui_load_asset(UI_ARCHIVE_FONT, file, NULL, bitmap);
}

vb2_error_t ui_get_step_icon_bitmap(int step, int focused,
				    struct ui_asset *bitmap)
{
	char file[UI_ASSET_FILENAME_MAX_LEN + 1];
	const char *pattern = focused ? "ic_%d-done.bmp" : "ic_%d.bmp";

	snprintf(file, sizeof(file), pattern, step);
	return ui_get_asset(file, NULL, 0, bitmap);
}
