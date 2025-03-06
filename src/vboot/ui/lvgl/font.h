/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __VBOOT_UI_LVGL_FONT_H__
#define __VBOOT_UI_LVGL_FONT_H__

#include <lvgl.h>

/*
 * Get font.
 *
 * @param font_name	Name of font.
 * @param font          Font to be filled.
 *
 * @return VB2_SUCCESS on success, non-zero on error.
 */
vb2_error_t ui_lvgl_get_font(const char *font_name, const lv_font_t **font);

#endif /* __VBOOT_UI_LVGL_FONT_H__ */
