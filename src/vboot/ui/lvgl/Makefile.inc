## SPDX-License-Identifier: GPL-2.0

LVGL_PATH = $(src)/3rdparty/lvgl
LVGL_CONFIG_H = $(src)/src/vboot/ui/lvgl/lv_conf.h
include $(LVGL_PATH)/lvgl.mk

LVGL_OBJS = $(patsubst $(LVGL_PATH)/%.c, $(obj)/lvgl/%.o, $(CSRCS))
LVGL_LIB = $(obj)/lvgl.a

lvgl-objs += $(LVGL_LIB)

depthcharge-y += components.c
depthcharge-y += display.c
depthcharge-y += font.c
depthcharge-y += layout.c

# Set lv_conf.h directory
CFLAGS += -DLV_CONF_PATH="$(LVGL_CONFIG_H)"

$(obj)/lvgl/%.o: $(LVGL_PATH)/%.c $(LVGL_CONFIG_H)
	@printf "    CC         $(subst $(obj)/,,$@)\n"
	mkdir -p $(dir $@)
	$(Q)$(XCC) -MMD $(CFLAGS) -c -o $@ $<

$(LVGL_LIB): $(LVGL_OBJS)
	@printf "    AR         $(subst $(obj)/,,$@)\n"
	$(Q)$(AR) qcT $@ $^
