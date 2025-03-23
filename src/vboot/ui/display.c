// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <libpayload.h>
#include <stdbool.h>
#include <vb2_api.h>

#include "drivers/ec/cros/ec.h"
#include "drivers/video/display.h"
#include "vboot/ui.h"

#define DEV_URL "google.com/chromeos/devmode"

static const struct ui_error_message errors[] = {
	[UI_ERROR_MINIOS_BOOT_FAILED] = {
		.file = "error_internet_recovery.bmp",
		.mesg = "Internet recovery partition corrupted or missing.\n"
			"Please recover using external storage instead.",
	},
	[UI_ERROR_DEV_MODE_ALREADY_ENABLED] = {
		.file = "error_dev_mode_enabled.bmp",
		.mesg = "Developer mode is already turned on.",
	},
	[UI_ERROR_UNTRUSTED_CONFIRMATION] = {
		.file = "error_untrusted_confirm.bmp",
		.mesg = "You cannot use an external keyboard to turn on\n"
			"developer mode. Please use the on-device buttons\n"
			"noted in the navigation instructions.",
	},
	[UI_ERROR_TO_NORM_NOT_ALLOWED] = {
		.file = "error_to_norm_not_allowed.bmp",
		.mesg = "Returning to secure mode disallowed by GBB flags.",
	},
	/* TODO(b/210875258): Create google.com/chromeos/blocked_devmode */
	[UI_ERROR_DEV_BOOT_NOT_ALLOWED] = {
		.file = "error_dev_boot_not_allowed.bmp",
		.show_dev_url = 1,
		.mesg = "Booting in developer mode is not allowed. For more\n"
			"info, visit: " DEV_URL,
	},
	[UI_ERROR_INTERNAL_BOOT_FAILED] = {
		.file = "error_int_boot_failed.bmp",
		.mesg = "Something went wrong booting from internal disk.\n"
			"View firmware log for details.",
	},
	[UI_ERROR_EXTERNAL_BOOT_DISABLED] = {
		.file = "error_ext_boot_disabled.bmp",
		.show_dev_url = 1,
		.mesg = "Booting from an external disk is disabled. For more\n"
			"info, visit: " DEV_URL,
	},
	[UI_ERROR_ALTFW_DISABLED] = {
		.file = "error_alt_boot_disabled.bmp",
		.show_dev_url = 1,
		.mesg = "Alternate bootloaders are disabled. For more info\n"
			"visit: " DEV_URL,
	},
	[UI_ERROR_ALTFW_EMPTY] = {
		.file = "error_no_alt_bootloader.bmp",
		.show_dev_url = 1,
		.mesg = "Could not find an alternate bootloader. To learn how\n"
			"to install one, visit: " DEV_URL,
	},
	[UI_ERROR_ALTFW_FAILED] = {
		.file = "error_alt_boot_failed.bmp",
		.mesg = "Something went wrong launching the alternate\n"
			"bootloader. View firmware log for details.",
	},
	[UI_ERROR_DEBUG_LOG] = {
		.file = "error_debug_info.bmp",
		.mesg = "Could not get debug info.",
	},
	[UI_ERROR_FIRMWARE_LOG] = {
		.file = "error_firmware_log.bmp",
		.mesg = "Could not get firmware log.",
	},
	[UI_ERROR_DIAGNOSTICS] = {
		.file = "error_diagnostics.bmp",
		.mesg = "Could not get diagnostic information.",
	},
};

static vb2_error_t init_screen(void)
{
	static int initialized = 0;
	if (initialized)
		return VB2_SUCCESS;

	/* Make sure framebuffer is initialized before turning display on. */
	clear_screen(&ui_color_black);
	if (display_init())
		return VB2_ERROR_UI_DISPLAY_INIT;

	enable_graphics_buffer();
	backlight_update(true);

	initialized = 1;
	return VB2_SUCCESS;
}

/*
 * Calculate the 32-bit value to report as the AP firmware state
 *
 * This must be stable for all time, since FAFT tests rely on it.
 *
 * @param state	UI state to report
 * @return corresponding 32-bit value
 */
static uint32_t calc_ap_fw_state(const struct ui_state *state)
{
	/*
	 * For now we only report the screen ID. At some point the focused_item
	 * could be added, but we may want to renumber the screens to take up
	 * less space, first.
	 *
	 * Current valid values are defined by enum ui_screen and currently use
	 * 11 bits.
	 */
	return state->screen->id;
}

vb2_error_t ui_display(struct ui_context *ui,
		       const struct ui_state *prev_state)
{
	vb2_error_t rv;
	const struct ui_state *state = ui->state;
	UI_INFO("screen=%#x, locale=%u, focused_item=%u, "
		"disabled_item_mask=%#x, hidden_item_mask=%#x, "
		"timer_disabled=%d, current_page=%u, error=%#x\n",
		state->screen->id, ui->state->locale->id, state->focused_item,
		state->disabled_item_mask, state->hidden_item_mask,
		state->timer_disabled, state->current_page, state->error_code);

	int32_t y = UI_BOX_MARGIN_V;
	const struct ui_screen_info *screen = state->screen;
	const struct ui_error_message *error = NULL;

	VB2_TRY(init_screen());

	if (state->error_code != UI_ERROR_NONE)
		error = &errors[state->error_code];

	/*
	 * Dim the screen.  Basically, if we're going to show a
	 * dialog, we need to dim the background colors so it's not so
	 * distracting.
	 */
	if (error)
		set_blend(&ui_color_black, ALPHA(60));

	if (screen->draw)
		rv = screen->draw(ui, prev_state);
	else
		rv = ui_draw_default(ui, prev_state);

	if (rv) {
		UI_ERROR("Drawing screen %#x failed: %#x\n", screen->id, rv);
		/* Print fallback message if drawing failed. */
		if (screen->mesg)
			ui_draw_textbox(screen->mesg, &y, 1);
		/* Also draw colored stripes */
		ui_draw_fallback_stripes(screen->id, state->focused_item);
	}
	/* Disable screen dimming. */
	if (error)
		clear_blend();
	/*
	 * If there's an error message to be printed, print it out.
	 * If we're already printing out a fallback message, give it
	 * priority and don't show the error box. Also, print out the
	 * error message to the AP console.
	 */
	if (rv == VB2_SUCCESS && error) {
		ui_draw_error_box(error, state);
		if (error->mesg)
			UI_WARN("%s\n", error->mesg);
	}

	flush_graphics_buffer();

	/*
	 * Tell the EC about our state...ignore errors since some ECs won't
	 * support this.
	 */
	if (CONFIG(DRIVER_EC_CROS))
		cros_ec_set_ap_fw_state(calc_ap_fw_state(ui->state));

	return rv;
}

int ui_display_clear(void)
{
	disable_graphics_buffer();
	return clear_screen(&ui_color_black);
}
