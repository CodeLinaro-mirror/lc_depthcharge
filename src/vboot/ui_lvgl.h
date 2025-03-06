/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __VBOOT_UI_LVGL_H__
#define __VBOOT_UI_LVGL_H__

#include <vb2_api.h>

#include "vboot/ui.h"

/******************************************************************************/
/* lvgl/display.c */

/*
 * Initialize LVGL display and buffers.
 *
 * @return VB2_SUCCESS, or error code on error.
 */
vb2_error_t ui_lvgl_init_display(void);

/*
 * Call LVGL timer handler. Function isolated from ui/loop.c
 * to prevent lvgl.h import.
 */
void ui_lvgl_call_timer_handler(void);

/*
 * Clean up LVGL.
 *
 * lvgl functions should no longer be called after this is called.
 */
void ui_lvgl_cleanup(void);

#endif /* __VBOOT_UI_LVGL__ */
