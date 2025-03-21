// SPDX-License-Identifier: GPL-2.0

#include <tests/test.h>
#include <vboot/ui.h>
#include <vb2_api.h>

vb2_error_t ui_get_asset(const char *name, const char *locale_code,
			 int focused, struct ui_asset *asset)
{
	return mock_type(vb2_error_t);
}

vb2_error_t ui_get_language_name_asset(const char *locale_code,
				       const char *ext,
				       struct ui_asset *asset)
{
	return mock_type(vb2_error_t);
}
