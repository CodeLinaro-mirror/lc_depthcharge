// SPDX-License-Identifier: GPL-2.0

#include <libpayload.h>
#include <lvgl.h>

#include "vboot/ui.h"
#include "vboot/ui/lvgl/font.h"
#include "vboot/ui/lvgl/layout.h"

/* TODO: Create proper color constants for styles. Styles are still unsupported.  */
static const uint32_t color_black = 0x000000;

static int32_t get_canvas_resolution(void)
{
	static int32_t res_cache = 0;
	if (res_cache)
		return res_cache;
	lv_display_t *display = lv_display_get_default();
	int32_t vert_res = lv_display_get_physical_vertical_resolution(display);
	int32_t hori_res = lv_display_get_physical_horizontal_resolution(display);
	res_cache = MIN(vert_res, hori_res);
	return res_cache;
}

static lv_obj_t *create_canvas(void)
{
	/*
	 * An invisible canvas is used to provide us an anchor for all the
	 * widgets within the screen.  The x and y units we use are always
	 * relative to the canvas whether or not the coordinate units are
	 * relative to UI_SCALE or in pixel units.
	 */
	int32_t canvas_res;
	lv_obj_t *canvas;

	/* Get size of canvas */
	canvas_res = get_canvas_resolution();

	/* Create canvas */
	canvas = lv_obj_create(lv_screen_active());
	if (!canvas)
		return NULL;

	lv_obj_set_size(canvas, canvas_res, canvas_res);
	lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);
	return canvas;
}

static lv_obj_t *get_canvas(void)
{
	return lv_obj_get_child(lv_screen_active(), 0);
}

int32_t ui_lvgl_to_pixels(int32_t val)
{
	int32_t canvas_res = get_canvas_resolution();
	return DIV_ROUND_UP((int64_t)val * canvas_res, UI_SCALE);
}

vb2_error_t ui_lvgl_get_scale(const char *font_name,
			      int32_t target_height,
			      int32_t *scale)
{
	const lv_font_t *font;
	*scale = LV_SCALE_NONE;
	VB2_TRY(ui_lvgl_get_font(font_name, &font));
	int32_t line_height_px = lv_font_get_line_height(font);
	int32_t canvas_res = get_canvas_resolution();
	*scale = DIV_ROUND_UP((int64_t)LV_SCALE_NONE * canvas_res * target_height,
			      (int64_t)UI_SCALE * line_height_px);
	return VB2_SUCCESS;
}

/*
 * Draw a text label.
 *
 * @param parent	Parent widget of the label.
 * @param file		File containing label text.
 * @param locale_code	Language code of locale.
 * @param x		X position within parent.
 * @param y		Y position within parent.
 * @param align		Alignment of text relative to parent.
 *			0 if no alignment.
 * @param out_label	Label object to be filled.
 *			Can be NULL if no need to filled.
 *
 * @return VB2_SUCCESS on success, non-zero on error.
 */
static vb2_error_t create_label(lv_obj_t *parent, const char *file,
				const char *locale_code,
				int32_t x, int32_t y,
				lv_align_t align,
				lv_obj_t **out_label)
{
	const char *text;
	lv_obj_t *label;

	/* Create text label */
	label = lv_label_create(parent);
	if (!label)
		return VB2_ERROR_UI_MEMORY_ALLOC;
	VB2_TRY(ui_get_text(file, locale_code, &text));
	lv_label_set_text(label, text);

	/* Set position */
	if (align)
		lv_obj_align(label, align, x, y);
	else
		lv_obj_set_pos(label, ui_lvgl_to_pixels(x), ui_lvgl_to_pixels(y));
	if (out_label)
		*out_label = label;
	return VB2_SUCCESS;
}

static vb2_error_t scale_label(lv_obj_t *label,
			       const char *font_name,
			       int32_t target_height)
{
	int32_t scale;
	VB2_TRY(ui_lvgl_get_scale(font_name, target_height, &scale));
	lv_obj_set_style_transform_scale(label, scale, LV_PART_MAIN | LV_STATE_DEFAULT);
	return VB2_SUCCESS;
}

static vb2_error_t scale_button(lv_obj_t *button,
				const char *font_name,
				int32_t target_height,
				int32_t box_width,
				int32_t box_height)
{
	/*
	 * To scale the text within the button, we must scale the entire button,
	 * because scaling just the text misaligns the centering of the label.
	 * Therefore, instead of setting the size as box_width and box_height,
	 * we must first scale the size by the inverse of text scaling ratio.
	 * We also convert it to pixel units (for higher precision).
	 *
	 * Let w be box_width. We want:
	 * scaled_w_px = w_px * (font_height / target_height)
	 *             = (w * res / UI_SCALE) * (font_height / target_height)
	 *	       = w * (font_height * res / UI_SCALE) / target_height
	 *             = w * font_height_px / target_height
	 * Same for box_height.
	 */
	int32_t scaled_w_px, scaled_h_px, scale;
	const lv_font_t *font;

	/* Scale button by inverse of text scaling ratio. */
	VB2_TRY(ui_lvgl_get_font(font_name,&font));
	int32_t font_height_px = lv_font_get_line_height(font);
	scaled_w_px = DIV_ROUND_UP((int64_t)box_width * font_height_px, target_height);
	scaled_h_px = DIV_ROUND_UP((int64_t)box_height * font_height_px, target_height);
	lv_obj_set_size(button, scaled_w_px, scaled_h_px);

	/* Scale to target height. */
	VB2_TRY(ui_lvgl_get_scale(font_name, target_height, &scale));
	lv_obj_set_style_transform_scale(button, scale, LV_PART_MAIN | LV_STATE_DEFAULT);
	return VB2_SUCCESS;
}

vb2_error_t ui_draw_language_header(const struct ui_locale *locale,
				    const struct ui_state *state,
				    int focused)
{
	lv_obj_t *header, *label;
	lv_obj_t *canvas = get_canvas();
	int32_t x, y, label_x_px;
	const char *language_name;

	const int32_t box_width = UI_LANG_ICON_GLOBE_SIZE +
		UI_LANG_ICON_MARGIN_H * 2 + UI_LANG_TEXT_WIDTH +
		UI_LANG_ICON_ARROW_SIZE + UI_LANG_ICON_MARGIN_H;
	const int32_t box_height = UI_LANG_BOX_HEIGHT;
	x = UI_MARGIN_H;
	y = UI_MARGIN_TOP;

	/* Create header button */
	header = lv_button_create(canvas);
	if (!header)
		return VB2_ERROR_UI_MEMORY_ALLOC;
	lv_obj_set_pos(header, ui_lvgl_to_pixels(x), ui_lvgl_to_pixels(y));

	/* TODO: Create proper style. */
	lv_obj_set_style_bg_color(header, lv_color_hex(color_black),
				  LV_PART_MAIN | LV_STATE_FOCUSED);

	/* Draw language text */
	label = lv_label_create(header);
	if (!label)
		return VB2_ERROR_UI_MEMORY_ALLOC;
	VB2_TRY(ui_get_language_name(locale->code, &language_name));
	lv_label_set_text(label, language_name);
	label_x_px = DIV_ROUND_UP(
		(int64_t)ui_lvgl_to_pixels(box_width) *
		(UI_LANG_ICON_GLOBE_SIZE + UI_LANG_ICON_MARGIN_H * 2), box_width);
	lv_obj_align(label, LV_ALIGN_LEFT_MID, label_x_px, 0);

	/* Scale header */
	VB2_TRY(scale_button(header, locale->font_name, UI_LANG_TEXT_HEIGHT,
			     box_width, box_height));

	if (focused)
		lv_group_focus_obj(header);
	return VB2_SUCCESS;
}

static vb2_error_t draw_title(const struct ui_context *ui, int32_t x, int32_t y)
{
	const struct ui_state *state = ui->state;
	const struct ui_screen_info *screen = state->screen;
	const struct ui_locale *locale = state->locale;
	lv_obj_t *label;
	lv_obj_t *canvas = get_canvas();
	VB2_TRY(create_label(canvas, screen->title, locale->code,
			     x, y, LV_ALIGN_DEFAULT, &label));
	VB2_TRY(scale_label(label, locale->font_name, UI_TITLE_TEXT_HEIGHT));

	return VB2_SUCCESS;
}

vb2_error_t ui_draw_desc(const struct ui_desc *desc,
			 const struct ui_state *state,
			 int32_t *y)
{
	lv_obj_t *label, *canvas;
	int i;
	int32_t x;
	const struct ui_locale *locale = state->locale;

	x = UI_MARGIN_H;
	canvas = get_canvas();
	for (i = 0; i < desc->count; i++) {
		if (i > 0)
			*y += UI_DESC_TEXT_LINE_SPACING;
		VB2_TRY(create_label(canvas, desc->files[i], locale->code,
				     x, *y, LV_ALIGN_DEFAULT, &label));
		VB2_TRY(scale_label(label, locale->font_name, UI_DESC_TEXT_HEIGHT));
		*y += UI_DESC_TEXT_HEIGHT;
	}

	return VB2_SUCCESS;
}

static vb2_error_t get_text_width(const char *text_file,
				  const char *locale_code,
				  const char *font_name,
				  int32_t *text_width)
{
	lv_point_t text_size_px;
	const lv_font_t *font;
	const char *text;
	int32_t font_height_px;

	VB2_TRY(ui_get_text(text_file, locale_code, &text));
	VB2_TRY(ui_lvgl_get_font(font_name, &font));
	lv_text_get_size(&text_size_px, text, font, 0, 0,
			 LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
	font_height_px = lv_font_get_line_height(font);
	/*
	 * Convert text width from px to relative units, and scale to target size.
	 * scaled_w = w * target_height / font_height
	 *          = (w_px * UI_SCALE / res) * target_height / font_height
	 *	    = w_px * target_height * (UI_SCALE / res * font_height)
	 *          = w_px * target_height / font_height_px
	 */
	*text_width = DIV_ROUND_UP((int64_t)text_size_px.x * UI_BUTTON_TEXT_HEIGHT,
				   font_height_px);
	return VB2_SUCCESS;
}

vb2_error_t ui_get_button_width(const struct ui_menu *menu,
				const struct ui_state *state,
				int32_t *button_width)
{
	const lv_font_t *font;
	int i;
	int32_t width, max_text_width;
	const struct ui_menu_item *item;
	const char *font_name = state->locale->font_name;
	const char *locale_code = state->locale->code;

	VB2_TRY(ui_lvgl_get_font(font_name, &font));
	max_text_width = 0;
	for (i = 0; i < menu->num_items; i++) {
		item = &menu->items[i];
		if (item->type != UI_MENU_ITEM_TYPE_PRIMARY)
			continue;
		VB2_TRY(get_text_width(item->file, locale_code, font_name, &width));
		max_text_width = MAX(width, max_text_width);
	}

	*button_width = max_text_width + UI_BUTTON_TEXT_PADDING_H * 2;
	return VB2_SUCCESS;
}

static vb2_error_t create_hidden_menu_button(void)
{
	/*
	 * Create a hidden widget. This hidden button is automatically added
	 * to the default lv_group. This is required in order to set up menu
	 * navigation properly because the state->focused_item and lv_group
	 * indices need to match.
	 */
	lv_obj_t *canvas = get_canvas();
	lv_obj_t *btn = lv_btn_create(canvas);
	if (!btn)
		return VB2_ERROR_UI_MEMORY_ALLOC;
	lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_STATE_DEFAULT);
	lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_width(btn, 0, LV_STATE_DEFAULT);
	return VB2_SUCCESS;
}

/*
 * Draw a primary button.
 *
 * @param item		Menu item.
 * @param state		UI state.
 * @param x		x-coordinate of the top-left corner.
 * @param y		y-coordinate of the top-left corner.
 * @param width		Relative width of button.
 * @param height	Relative height of button.
 * @param focused       True if button is on focus. Otherwise, False.
 *
 * @return VB2_SUCCESS on success, non-zero on error.
 */
static vb2_error_t draw_button(const struct ui_menu_item *item,
			       const struct ui_state *state,
			       int32_t x, int32_t y,
			       int32_t width, int32_t height,
			       bool focused)
{
	lv_obj_t *btn;
	lv_obj_t *canvas = get_canvas();
	const struct ui_locale *locale = state->locale;

	/* Draw button */
	btn = lv_button_create(canvas);
	if (!btn)
		return VB2_ERROR_UI_MEMORY_ALLOC;
	lv_obj_set_pos(btn, ui_lvgl_to_pixels(x), ui_lvgl_to_pixels(y));
	VB2_TRY(create_label(btn, item->file, locale->code,
			     0, 0, LV_ALIGN_CENTER, NULL));

	/* TODO: Create proper style. */
	lv_obj_set_style_bg_color(btn, lv_color_hex(color_black),
				  LV_PART_MAIN | LV_STATE_FOCUSED);

	/* Zoom button */
	VB2_TRY(scale_button(btn, locale->font_name, UI_BUTTON_TEXT_HEIGHT,
			     width, height));

	if (focused)
		lv_group_focus_obj(btn);
	return VB2_SUCCESS;
}

/*
 * Draw a link button, where the style is different from a primary button.
 *
 * @param item		Menu item.
 * @param state		UI state.
 * @param x		x-coordinate of the top-left corner.
 * @param y		y-coordinate of the top-left corner.
 * @param focused       True if link is on focus. Otherwise, False.
 *
 * @return VB2_SUCCESS on success, non-zero on error.
 */
static vb2_error_t draw_link(const struct ui_menu_item *item,
			     const struct ui_state *state,
			     int32_t x, int32_t y,
			     bool focused)
{
	lv_obj_t *btn;
	lv_obj_t *canvas = get_canvas();
	int32_t button_width, text_width, label_x_px;
	const char *locale_code = state->locale->code;
	const char *font_name = state->locale->font_name;

	/* Get button width */
	VB2_TRY(get_text_width(item->file, locale_code, font_name, &text_width));
	button_width = UI_LINK_TEXT_PADDING_LEFT + UI_LINK_ICON_SIZE +
		UI_LINK_ICON_MARGIN_R + text_width + UI_LINK_ARROW_MARGIN_H;

	/* Draw link */
	btn = lv_button_create(canvas);
	if (!btn)
		return VB2_ERROR_UI_MEMORY_ALLOC;
	lv_obj_set_pos(btn, ui_lvgl_to_pixels(x), ui_lvgl_to_pixels(y));
	label_x_px = DIV_ROUND_UP(
		(int64_t)ui_lvgl_to_pixels(button_width) *
		(UI_LINK_TEXT_PADDING_LEFT + UI_LINK_ICON_SIZE + UI_LINK_ICON_MARGIN_R),
		button_width);
	VB2_TRY(create_label(btn, item->file, locale_code, label_x_px, 0,
			     LV_ALIGN_LEFT_MID, NULL));

	/* TODO: Create proper style. */
	lv_obj_set_style_bg_color(btn, lv_color_hex(color_black),
				  LV_PART_MAIN | LV_STATE_FOCUSED);

	/* Zoom link */
	VB2_TRY(scale_button(btn, font_name, UI_BUTTON_TEXT_HEIGHT,
			     button_width, UI_BUTTON_HEIGHT));

	if (focused)
		lv_group_focus_obj(btn);
	return VB2_SUCCESS;
}

vb2_error_t ui_draw_menu_items(const struct ui_menu *menu,
			       const struct ui_state *state,
			       const struct ui_state *prev_state,
			       int32_t y)
{
	/*
	 * ui_draw_default() calls lv_group_get_obj_by_index() to get button obj
	 * under focus. Therefore we need to create buttons in the same order as
	 * the menu items list to assign them the correct indices within the group.
	 * Hence, we draw both primary and secondary buttons from top to bottom.
	 */
	int i;
	int32_t x, button_width;
	const struct ui_menu_item *item;
	size_t num_links = 0;
	bool hidden;

	/* Primary buttons */
	x = UI_MARGIN_H;
	VB2_TRY(ui_get_button_width(menu, state, &button_width));
	for (i = 0; i < menu->num_items; i++) {
		item = &menu->items[i];
		hidden = UI_GET_BIT(state->hidden_item_mask, i);
		if (item->type == UI_MENU_ITEM_TYPE_SECONDARY && !hidden) {
			num_links++;
			continue;
		}
		if (item->type != UI_MENU_ITEM_TYPE_PRIMARY)
			continue;
		if (hidden) {
			create_hidden_menu_button();
			continue;
		}

		VB2_TRY(draw_button(item, state, x, y, button_width,
				    UI_BUTTON_HEIGHT, i == state->focused_item));

		y += UI_BUTTON_HEIGHT + UI_BUTTON_MARGIN_V;
	}

	/* Secondary (link) buttons */
	x = UI_MARGIN_H - UI_LINK_TEXT_PADDING_LEFT;
	y = UI_SCALE - UI_MARGIN_BOTTOM - UI_FOOTER_HEIGHT -
	    UI_FOOTER_MARGIN_TOP - UI_BUTTON_HEIGHT -
	    num_links * (UI_BUTTON_HEIGHT + UI_BUTTON_MARGIN_V);
	for (i = 0; i < menu->num_items; i++) {
		item = &menu->items[i];
		if (item->type != UI_MENU_ITEM_TYPE_SECONDARY)
			continue;
		if (UI_GET_BIT(state->hidden_item_mask, i)) {
			create_hidden_menu_button();
			continue;
		}

		VB2_TRY(draw_link(item, state, x, y, i == state->focused_item));

		y += UI_BUTTON_HEIGHT + UI_BUTTON_MARGIN_V;
	}

	return VB2_SUCCESS;
}

static vb2_error_t refresh_screen(const struct ui_state *state)
{
	const lv_font_t *font;
	lv_obj_t *canvas;
	lv_group_t *group;

	canvas = get_canvas();
	if (!canvas) {
		canvas = create_canvas();
		if (!canvas)
			return VB2_ERROR_UI_MEMORY_ALLOC;
	}
	lv_obj_clean(canvas);

	/* Set default font for entire canvas */
	VB2_TRY(ui_lvgl_get_font(state->locale->font_name, &font));
	lv_obj_set_style_text_font(canvas, font, LV_PART_MAIN | LV_STATE_DEFAULT);

	/* Refresh group objects */
	group = lv_group_get_default();
	if (!group) {
		group = lv_group_create();
		if (!group)
			return VB2_ERROR_UI_MEMORY_ALLOC;
		lv_group_set_default(group);
	}
	lv_group_remove_all_objs(group);

	return VB2_SUCCESS;
}

vb2_error_t ui_draw_default(struct ui_context *ui,
			    const struct ui_state *prev_state)
{
	int32_t x, y;
	bool refresh_all;
	const struct ui_state *state = ui->state;
	const struct ui_screen_info *screen = state->screen;
	const struct ui_menu *menu = ui_get_menu(ui);

	/*
	 * Refresh the whole screen if previous drawing failed, there
	 * is no previous screen, locale changed, or screen changed.
	 */
	refresh_all = !prev_state || prev_state->locale != state->locale ||
		      prev_state->error_code != state->error_code ||
		      prev_state->screen != state->screen;

	if (!refresh_all) {
		if (state->focused_item != prev_state->focused_item) {
			/* Update focused button. */
			lv_group_focus_obj(lv_group_get_obj_by_index(lv_group_get_default(),
								     state->focused_item));
		}
		return VB2_SUCCESS;
	}

	VB2_TRY(refresh_screen(state));

	/* Language dropdown header */
	if (menu->num_items > 0 &&
	    menu->items[0].type == UI_MENU_ITEM_TYPE_LANGUAGE) {
		VB2_TRY(ui_draw_language_header(state->locale, state,
						state->focused_item == 0));
	}

	x = UI_MARGIN_H;
	if (screen->is_fullview)
		y = UI_FULLVIEW_TITLE_MARGIN;
	else
		y = UI_MARGIN_TOP + UI_LANG_BOX_HEIGHT + UI_LANG_MARGIN_BOTTOM;

	/* Icon */
	if (screen->icon != UI_ICON_TYPE_NONE)
		y += UI_ICON_HEIGHT + UI_ICON_MARGIN_BOTTOM;

	/* Title */
	int32_t title_text_height, title_margin_bottom;

	if (screen->is_fullview) {
		title_text_height = UI_FULLVIEW_TITLE_TEXT_HEIGHT;
		title_margin_bottom = UI_FULLVIEW_TITLE_MARGIN;
	} else {
		title_text_height = UI_TITLE_TEXT_HEIGHT;
		title_margin_bottom = UI_TITLE_MARGIN_BOTTOM;
	}

	VB2_TRY(draw_title(ui, x, y));
	y += title_text_height + title_margin_bottom;

	if (screen->draw_desc)
		VB2_TRY(screen->draw_desc(ui, prev_state, &y));
	else
		VB2_TRY(ui_draw_desc(&screen->desc, state, &y));
	y += UI_DESC_MARGIN_BOTTOM;

	/* Primary and secondary buttons */
	if (screen->draw_menu_items)
		VB2_TRY(screen->draw_menu_items(ui, prev_state));
	else
		VB2_TRY(ui_draw_menu_items(menu, state, prev_state, y));
	return VB2_SUCCESS;
}


vb2_error_t ui_draw_textbox(const char *str, int32_t *y, int32_t min_lines)
{
	return UI_LVGL_NOT_IMPLEMENTED();
}

vb2_error_t ui_get_log_textbox_dimensions(enum ui_screen screen,
					  const char *locale_code,
					  uint32_t *lines_per_page,
					  uint32_t *chars_per_line)
{
	return UI_LVGL_NOT_IMPLEMENTED();
}

vb2_error_t ui_draw_log_textbox(const char *str, const struct ui_state *state,
				int32_t *y)
{
	return UI_LVGL_NOT_IMPLEMENTED();
}

vb2_error_t ui_draw_error_box(const struct ui_error_message *error,
			      const struct ui_state *state)
{
	return UI_LVGL_NOT_IMPLEMENTED();
}

void ui_draw_fallback_stripes(enum ui_screen screen,
			      uint32_t focused_item)
{
	UI_LVGL_NOT_IMPLEMENTED();
}
