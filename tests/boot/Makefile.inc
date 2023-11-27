# SPDX-License-Identifier: GPL-2.0

tests-y += bootconfig-test memchipinfo-test

bootconfig-test-srcs += tests/boot/bootconfig-test.c
bootconfig-test-srcs += src/boot/bootconfig.c

memchipinfo-test-srcs += tests/boot/memchipinfo.c
memchipinfo-test-srcs += tests/helpers/device_tree.c
memchipinfo-test-srcs += src/base/device_tree.c
memchipinfo-test-srcs += src/base/list.c
