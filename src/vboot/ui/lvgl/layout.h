/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __VBOOT_UI_LVGL_LAYOUT_H__
#define __VBOOT_UI_LVGL_LAYOUT_H__

#define UI_LVGL_NOT_IMPLEMENTED()				\
	({                                                      \
		UI_WARN("%s: not implemented\n", __func__);     \
		VB2_ERROR_UI_DRAW_FAILURE;                      \
	})

/*
 * Convert a value in relative scale to pixel units.
 *
 * @param val		Value to convert.
 *
 * @return Converted value in pixels.
 */
int32_t ui_lvgl_to_pixels(int32_t val);

/*
 * Get scaling constant for LVGL widgets with text.
 *
 * @param font_name	Font name of text on widget.
 * @param target_height	Target text height after scaling.
 * @param scale		Integer scale to be filled for
 *			lv_obj_set_style_transform_scale().
 *
 * @return VB2_SUCCESS on success, non-zero on error.
 */
vb2_error_t ui_lvgl_get_scale(const char *font_name,
			      int32_t target_height,
			      int32_t *scale);

#endif /* __VBOOT_UI_LVGL_LAYOUT_H__ */
