/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LVGL_STDINT_FIXUP_H__
#define __LVGL_STDINT_FIXUP_H__

#include <stdint.h>

/* TODO: lvgl uses 's8' and 's32' as variable names. Fix lvgl upstream. */
#define s8 _local_s8
#define s32 _local_s32

#endif /* __LVGL_STDINT_FIXUP_H__ */
