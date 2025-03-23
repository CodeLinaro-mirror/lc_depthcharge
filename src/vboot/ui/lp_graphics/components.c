// SPDX-License-Identifier: GPL-2.0

#include <libpayload.h>
#include <vb2_api.h>

#include "drivers/ec/cros/ec.h"
#include "vboot/ui.h"

#define UI_DESC(a) ((struct ui_desc){	\
	.count = ARRAY_SIZE(a),		\
	.files = a,			\
})

/******************************************************************************/
/* Log screens */

vb2_error_t ui_draw_log_desc(struct ui_context *ui,
			     const struct ui_state *prev_state,
			     int32_t *y)
{
	static char *prev_buf;
	static size_t prev_buf_len;
	static int32_t prev_y;
	const struct ui_state *state = ui->state;
	char *buf;
	size_t buf_len;
	vb2_error_t rv = VB2_SUCCESS;

	buf = ui_log_get_page_content(&state->log, state->current_page);
	if (!buf)
		return VB2_ERROR_UI_LOG_INIT;
	buf_len = strlen(buf);
	/* Redraw only if screen or text changed. */
	if (!prev_state || state->screen->id != prev_state->screen->id ||
	    state->error_code != prev_state->error_code || !prev_buf ||
	    buf_len != prev_buf_len || strncmp(buf, prev_buf, buf_len) ||
	    state->current_page != prev_state->current_page)
		rv = ui_draw_log_textbox(buf, state, y);
	else
		*y = prev_y;

	if (prev_buf)
		free(prev_buf);
	prev_buf = buf;
	prev_buf_len = buf_len;
	prev_y = *y;

	return rv;
}

/******************************************************************************/
/* UI_SCREEN_LANGUAGE_SELECT */

vb2_error_t ui_draw_language_select_menu(struct ui_context *ui,
					 const struct ui_state *prev_state)
{
	int id;
	const struct ui_state *state = ui->state;
	const int reverse = state->locale->rtl;
	uint32_t num_lang;
	uint32_t locale_id;
	int32_t x, x_begin, x_end, y, y_begin, y_end, y_center, menu_height;
	int num_lang_per_page, target_pos, id_begin, id_end;
	int32_t box_width, box_height;
	const int32_t border_thickness = UI_LANG_MENU_BORDER_THICKNESS;
	const uint32_t flags = PIVOT_H_LEFT | PIVOT_V_CENTER;
	int focused;
	const struct ui_locale *locale;
	const struct rgb_color *bg_color, *fg_color;
	struct ui_asset bitmap;

	num_lang = ui_get_locale_count();
	if (num_lang == 0) {
		UI_ERROR("Locale count is 0\n");
		return VB2_ERROR_UI_INVALID_ARCHIVE;
	}

	x_begin = UI_MARGIN_H;
	x_end = UI_SCALE - UI_MARGIN_H;
	box_width = x_end - x_begin;
	box_height = UI_LANG_MENU_BOX_HEIGHT;

	y_begin = UI_MARGIN_TOP + UI_LANG_BOX_HEIGHT + UI_LANG_MENU_MARGIN_TOP;
	y_end = UI_SCALE - UI_MARGIN_BOTTOM - UI_FOOTER_HEIGHT -
		UI_FOOTER_MARGIN_TOP;
	num_lang_per_page = (y_end - y_begin) / box_height;
	menu_height = box_height * MIN(num_lang_per_page, num_lang);
	y_end = y_begin + menu_height;  /* Correct for integer division error */

	/* Get current locale_id */
	locale_id = state->focused_item;
	if (locale_id >= num_lang) {
		UI_WARN("focused_item (%u) exceeds number of locales (%u); "
			"falling back to locale 0\n",
			locale_id, num_lang);
		locale_id = 0;
	}

	/* Draw language dropdown */
	VB2_TRY(ui_get_locale_info(locale_id, &locale));
	VB2_TRY(ui_draw_language_header(locale, state, 1));

	/*
	 * Calculate the list of languages to display, from id_begin
	 * (inclusive) to id_end (exclusive). The focused one is placed at the
	 * center of the list if possible.
	 */
	target_pos = (num_lang_per_page - 1) / 2;
	if (locale_id < target_pos || num_lang < num_lang_per_page) {
		/* locale_id is too small to put at the center, or
		   all languages fit in the screen */
		id_begin = 0;
		id_end = MIN(num_lang_per_page, num_lang);
	} else if (locale_id > num_lang - num_lang_per_page + target_pos) {
		/* locale_id is too large to put at the center */
		id_begin = num_lang - num_lang_per_page;
		id_end = num_lang;
	} else {
		/* Place locale_id at the center. It's guaranteed that
		   (id_begin >= 0) and (id_end <= num_lang). */
		id_begin = locale_id - target_pos;
		id_end = locale_id + num_lang_per_page - target_pos;
	}

	/* Draw dropdown menu */
	x = x_begin + UI_LANG_ICON_GLOBE_SIZE + UI_LANG_ICON_MARGIN_H * 2;
	y = y_begin;
	for (id = id_begin; id < id_end; id++) {
		focused = id == locale_id;
		bg_color = focused ? &ui_color_button : &ui_color_lang_menu_bg;
		fg_color = focused ? &ui_color_lang_menu_bg : &ui_color_fg;
		/* Solid box */
		VB2_TRY(ui_draw_rounded_box(x_begin, y, box_width, box_height,
					    bg_color, 0, 0, reverse));
		/* Separator between languages */
		if (id > id_begin)
			VB2_TRY(ui_draw_h_line(x_begin, y, box_width,
					       border_thickness,
					       &ui_color_lang_menu_border));
		/* Text */
		y_center = y + box_height / 2;
		VB2_TRY(ui_get_locale_info(id, &locale));
		VB2_TRY(ui_get_language_name_bitmap(locale->code, &bitmap));
		VB2_TRY(ui_draw_mapped_bitmap(&bitmap, x, y_center,
					      UI_SIZE_AUTO,
					      UI_LANG_MENU_TEXT_HEIGHT,
					      bg_color, fg_color,
					      flags, reverse));
		y += box_height;
	}

	/* Draw outer borders */
	VB2_TRY(ui_draw_rounded_box(x_begin, y_begin, box_width, menu_height,
				    &ui_color_lang_menu_border,
				    border_thickness, 0, reverse));

	if (num_lang <= num_lang_per_page)
		return VB2_SUCCESS;

	/* Draw scrollbar */
	x = x_end - UI_LANG_MENU_SCROLLBAR_MARGIN_RIGHT - UI_SCROLLBAR_WIDTH;
	if (reverse)
		x = UI_SCALE - x - UI_SCROLLBAR_WIDTH +
		    UI_LANG_MENU_SCROLLBAR_MARGIN_RIGHT;
	VB2_TRY(ui_draw_scrollbar(x, y_begin, menu_height, id_begin, num_lang,
				  num_lang_per_page));

	return VB2_SUCCESS;
}

/******************************************************************************/
/* UI_SCREEN_DEVELOPER_MODE */

vb2_error_t ui_draw_developer_mode_desc(struct ui_context *ui,
					const struct ui_state *prev_state,
					int32_t *y)
{
	struct ui_asset bitmap;
	const struct ui_state *state = ui->state;
	const char *locale_code = state->locale->code;
	const int reverse = state->locale->rtl;
	int32_t x;
	const int32_t w = UI_SIZE_AUTO;
	int32_t h;
	uint32_t flags = PIVOT_H_LEFT | PIVOT_V_TOP;

	x = UI_MARGIN_H;

	/*
	 * Description about returning to secure mode. When developer mode is
	 * forced by GBB flags, hide this description line.
	 */
	if (!(vb2api_gbb_get_flags(ui->ctx) &
	      VB2_GBB_FLAG_FORCE_DEV_SWITCH_ON)) {
		VB2_TRY(ui_get_asset("dev_desc0.bmp", locale_code, 0, &bitmap));
		h = UI_DESC_TEXT_HEIGHT * ui_get_bitmap_num_lines(&bitmap);
		VB2_TRY(ui_draw_bitmap(&bitmap, x, *y, w, h, flags, reverse));
		*y += h + UI_DESC_TEXT_LINE_SPACING;
	}

	/*
	 * Description about automatically booting from the default boot target.
	 * After the timer in developer mode is disabled, this description no
	 * longer makes sense, so hide it.
	 */
	VB2_TRY(ui_get_asset("dev_desc1.bmp", locale_code, 0, &bitmap));
	h = UI_DESC_TEXT_HEIGHT * ui_get_bitmap_num_lines(&bitmap);
	/* Either clear the desc line, or draw it again. */
	if (state->timer_disabled)
		VB2_TRY(ui_draw_box(x, *y, UI_SCALE - x, h, &ui_color_bg,
				    reverse));
	else
		VB2_TRY(ui_draw_bitmap(&bitmap, x, *y, w, h, flags, reverse));
	*y += h;

	return VB2_SUCCESS;
}

/******************************************************************************/
/* UI_SCREEN_DIAGNOSTICS_STORAGE_TEST_SHORT */
/* UI_SCREEN_DIAGNOSTICS_STORAGE_TEST_EXTENDED */

vb2_error_t ui_diagnostics_test_back_get_width(const struct ui_state *state,
					       int32_t *width)
{
	const char *const files[] = {
		UI_DIAGNOSTICS_TEST_BACK_FILE,
		UI_DIAGNOSTICS_TEST_CANCEL_FILE,
	};

	*width = 0;
	for (int i = 0; i < ARRAY_SIZE(files); i++) {
		struct ui_asset bitmap;
		int32_t button_width;
		VB2_TRY(ui_get_asset(files[i], state->locale->code, 0,
				     &bitmap));
		VB2_TRY(ui_get_bitmap_width(&bitmap, UI_BUTTON_TEXT_HEIGHT,
					    &button_width));
		*width = MAX(*width, button_width);
	}

	return VB2_SUCCESS;
}
