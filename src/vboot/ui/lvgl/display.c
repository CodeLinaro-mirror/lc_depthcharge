// SPDX-License-Identifier: GPL-2.0

#include <libpayload.h>
#include <lvgl.h>
#include <stdbool.h>
#include <vb2_api.h>

#include "drivers/video/display.h"
#include "vboot/ui.h"
#include "vboot/ui_lvgl.h"

static lv_display_rotation_t get_display_rotation(const struct cb_framebuffer *fbinfo)
{
	switch (fbinfo->orientation) {
	case CB_FB_ORIENTATION_NORMAL:
	default:
		return LV_DISPLAY_ROTATION_0;
	case CB_FB_ORIENTATION_LEFT_UP:
		return LV_DISPLAY_ROTATION_90;
	case CB_FB_ORIENTATION_BOTTOM_UP:
		return LV_DISPLAY_ROTATION_180;
	case CB_FB_ORIENTATION_RIGHT_UP:
		return LV_DISPLAY_ROTATION_270;
	}
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
	const struct cb_framebuffer *fbinfo = &lib_sysinfo.framebuffer;
	void *fb = (void *)phys_to_virt(fbinfo->physical_address);
	uint32_t bpl = fbinfo->bytes_per_line;
	uint32_t bpp = fbinfo->bits_per_pixel / 8;
	size_t x_size = (area->x2 - area->x1 + 1) * bpp;
	int32_t y;
	for (y = area->y1; y <= area->y2; y++) {
		void *line = fb + y * bpl;
		memcpy(line + area->x1 * bpp, px_map, x_size);
		px_map += x_size;
	}
	lv_display_flush_ready(disp);
}

vb2_error_t ui_lvgl_init_display(void)
{
	lv_init();
	lv_tick_set_cb(vb2ex_mtime);

	/* Set up LVGL display */
	const struct cb_framebuffer *fbinfo = &lib_sysinfo.framebuffer;
	if (!fbinfo->physical_address) {
		UI_ERROR("No framebuffer\n");
		return VB2_ERROR_UI_DISPLAY_INIT;
	}
	UI_INFO("Screen resolution: %u, %u\n", fbinfo->x_resolution, fbinfo->y_resolution);
	lv_display_t *display = lv_display_create(fbinfo->x_resolution, fbinfo->y_resolution);
	if (fbinfo->bits_per_pixel != LV_COLOR_DEPTH) {
		UI_ERROR("bits_per_pixel (%u) != LV_COLOR_DEPTH (%u)\n", fbinfo->bits_per_pixel,
			 LV_COLOR_DEPTH);
		return VB2_ERROR_UI_DISPLAY_INIT;
	}

	lv_display_set_rotation(display, get_display_rotation(fbinfo));

	/* Flush */
	lv_display_set_flush_cb(display, flush_cb);

	/* Buffer */
	uint32_t buf_size = (uint64_t)fbinfo->x_resolution * fbinfo->y_resolution / 10 *
			    (fbinfo->bits_per_pixel / 8);
	void *buf = xmalloc(buf_size);
	lv_display_set_buffers(display, buf, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_default(display);

	return VB2_SUCCESS;
}

void ui_lvgl_call_timer_handler(void)
{
	lv_timer_handler();
}

void ui_lvgl_cleanup(void)
{
	lv_deinit();
}
