// SPDX-License-Identifier: GPL-2.0

#include <libpayload.h>
#include <lvgl.h>
#include <vb2_api.h>

#include "vboot/ui.h"
#include "vboot/ui_lvgl.h"
#include "vboot/ui/lvgl/layout.h"

/******************************************************************************/
/* Log screens */

vb2_error_t ui_draw_log_desc(struct ui_context *ui,
			     const struct ui_state *prev_state,
			     int32_t *y)
{
	return UI_LVGL_NOT_IMPLEMENTED();
}

/******************************************************************************/
/* UI_SCREEN_LANGUAGE_SELECT */

vb2_error_t ui_draw_language_select_menu(struct ui_context *ui,
					 const struct ui_state *prev_state)
{
	return UI_LVGL_NOT_IMPLEMENTED();
}

/******************************************************************************/
/* UI_SCREEN_DEVELOPER_MODE */

static void timer_cb_hide_dev_desc(lv_timer_t *timer)
{
	/* Hide label if timer requirement is met */
	lv_obj_t *label = lv_timer_get_user_data(timer);
	struct ui_context *ui = lv_obj_get_user_data(label);
	if (ui->state->timer_disabled) {
		lv_obj_del(label);
		lv_timer_set_repeat_count(timer, 0);
	}
}

vb2_error_t ui_draw_developer_mode_desc(struct ui_context *ui,
					const struct ui_state *prev_state,
					int32_t *y)
{
	int32_t x, x_px, y_px, scale;
	const struct ui_state *state = ui->state;
	const struct ui_locale *locale = state->locale;
	const char *dev_desc0, *dev_desc1;
	lv_obj_t *label_desc_0, *label_desc_1;
	lv_obj_t *canvas = lv_obj_get_child(lv_screen_active(), 0);

	VB2_TRY(ui_lvgl_get_scale(locale->font_name,
				  UI_DESC_TEXT_HEIGHT,
				  &scale));
	x = UI_MARGIN_H;
	/*
	 * Description about returning to secure mode. When developer mode is
	 * forced by GBB flags, hide this description line.
	 */
	if (!(vb2api_gbb_get_flags(ui->ctx) & VB2_GBB_FLAG_FORCE_DEV_SWITCH_ON)) {
		/* Display label */
		label_desc_0 = lv_label_create(canvas);
		VB2_TRY(ui_get_text("dev_desc0.txt", locale->code, &dev_desc0));
		lv_label_set_text(label_desc_0, dev_desc0);
		x_px = ui_lvgl_to_pixels(x);
		y_px = ui_lvgl_to_pixels(*y);
		lv_obj_set_pos(label_desc_0, x_px, y_px);
		lv_obj_set_style_transform_scale(label_desc_0, scale, 0);
		*y += UI_DESC_TEXT_HEIGHT + UI_DESC_TEXT_LINE_SPACING;
	}
	/*
	 * Description about automatically booting from the default boot target.
	 * After the timer in developer mode is disabled, this description no
	 * longer makes sense, so hide it.
	 *
	 * Draw label and create an LVGL-managed timer to check its existence.
	 */
	label_desc_1 = lv_label_create(canvas);
	VB2_TRY(ui_get_text("dev_desc1.txt", locale->code, &dev_desc1));
	lv_label_set_text(label_desc_1, dev_desc1);
	lv_obj_set_user_data(label_desc_1, ui);
	x_px = ui_lvgl_to_pixels(x);
	y_px = ui_lvgl_to_pixels(*y);
	lv_obj_set_pos(label_desc_1, x_px, y_px);
	lv_obj_set_style_transform_scale(label_desc_1, scale, 0);
	lv_timer_create(timer_cb_hide_dev_desc, 0, label_desc_1);
	*y += UI_DESC_TEXT_HEIGHT;

	return VB2_SUCCESS;
}

/******************************************************************************/
/* UI_SCREEN_DIAGNOSTICS_STORAGE_TEST_SHORT */
/* UI_SCREEN_DIAGNOSTICS_STORAGE_TEST_EXTENDED */

vb2_error_t ui_diagnostics_test_back_get_width(const struct ui_state *state,
					       int32_t *width)
{
	return UI_LVGL_NOT_IMPLEMENTED();
}
