################################################################################
# SPDX-License-Identifier: LGPL-3.0-only
#
# This file is part of nlif.
# Copyright (C) 2026 Grégor Boirie <gregor.boirie@free.fr>
################################################################################

common-cflags       := -Wall \
                       -Wextra \
                       -Wformat=2 \
                       -Wundef \
                       -Wshadow \
                       -Wcast-align \
                       -Wmissing-declarations \
                       -D_GNU_SOURCE \
                       $(EXTRA_CFLAGS) \
                       -I $(TOPDIR) \
                       -fvisibility=internal

common-ldflags      := $(common-cflags) $(EXTRA_LDFLAGS) \
                       -Wl,--as-needed \
                       -Wl,-z,start-stop-visibility=internal

ifneq ($(filter y,$(CONFIG_SRPLUG_ASSERT)),)
common-cflags       := $(filter-out -DNDEBUG,$(common-cflags))
common-ldflags      := $(filter-out -DNDEBUG,$(common-ldflags))
endif # ($(filter y,$(CONFIG_SRPLUG_ASSERT)),)

arlibs              := libsrplug.a
libsrplug.a-objs    := static/common.o
libsrplug.a-objs    += $(call kconf_enabled,SRPLUG_DAEMON,static/daemon.o)
libsrplug.a-objs    += $(call kconf_enabled,SRPLUG_THREAD,static/thread.o)
libsrplug.a-cflags  := $(common-cflags)
libsrplug.a-cflags  += $(call kconf_enabled,SRPLUG_THREAD,-pthread)
libsrplug.a-pkgconf := $(call kconf_enabled,SRPLUG_DAEMON,libelog) \
                       libetux_timer_list libstroll

# ex: filetype=make :
