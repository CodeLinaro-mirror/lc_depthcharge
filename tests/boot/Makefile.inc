# SPDX-License-Identifier: GPL-2.0

tests-y += bootconfig-test commandline-test memchipinfo-test

bootconfig-test-srcs += tests/boot/bootconfig-test.c
bootconfig-test-srcs += src/boot/bootconfig.c

commandline-test-srcs += tests/boot/commandline-test.c
commandline-test-srcs += src/boot/commandline.c

memchipinfo-test-srcs += tests/boot/memchipinfo.c
memchipinfo-test-srcs += tests/helpers/device_tree.c
memchipinfo-test-srcs += src/base/device_tree.c
memchipinfo-test-srcs += src/base/list.c
