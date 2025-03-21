// SPDX-License-Identifier: GPL-2.0

#include <tests/test.h>
#include <vboot/ui.h>
#include <vb2_api.h>

vb2_error_t ui_get_language_name_bitmap(const char *locale_code,
					struct ui_asset *bitmap)
{
	return mock_type(vb2_error_t);
}

vb2_error_t ui_get_char_bitmap(const char c, struct ui_asset *bitmap)
{
	return mock_type(vb2_error_t);
}

vb2_error_t ui_get_step_icon_bitmap(int step, int focused,
				    struct ui_asset *bitmap)
{
	return mock_type(vb2_error_t);
}
