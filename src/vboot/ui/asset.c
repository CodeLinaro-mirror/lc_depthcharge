// SPDX-License-Identifier: GPL-2.0

#include <libpayload.h>
#include <string.h>
#include <vb2_api.h>

#include "vboot/ui.h"

vb2_error_t ui_get_asset(const char *name, const char *locale_code,
			 int focused, struct ui_asset *asset)
{
	char file[UI_ASSET_FILENAME_MAX_LEN + 1];
	const char *file_ext;
	const char *suffix = focused ? "_focus" : "";
	const size_t name_len = strlen(name);

	if (name_len + strlen(suffix) >= sizeof(file)) {
		UI_ERROR("Name %s too long\n", name);
		return VB2_ERROR_INVALID_PARAMETER;
	}

	file_ext = strrchr(name, '.');
	if (file_ext)
		snprintf(file, sizeof(file), "%.*s%s%s",
			 (int)(file_ext - name), name, suffix,
			 file_ext);
	else
		snprintf(file, sizeof(file), "%s%s", name, suffix);

	return ui_load_asset(
		locale_code ? UI_ARCHIVE_LOCALIZED : UI_ARCHIVE_GENERIC,
		file, locale_code, asset);
}

vb2_error_t ui_get_language_name_asset(const char *locale_code,
				       const char *ext,
				       struct ui_asset *asset)
{
	char file[UI_ASSET_FILENAME_MAX_LEN + 1];
	char pattern[] = "language_%s.%s";

	snprintf(file, sizeof(file), pattern, locale_code, ext);
	return ui_get_asset(file, NULL, 0, asset);
}
