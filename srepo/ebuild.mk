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

ifneq ($(filter y,$(CONFIG_SREPO_ASSERT)),)
common-cflags       := $(filter-out -DNDEBUG,$(common-cflags))
common-ldflags      := $(filter-out -DNDEBUG,$(common-ldflags))
endif # ($(filter y,$(CONFIG_SREPO_ASSERT)),)

arlibs             := libsrepo.a
libsrepo.a-objs    := static/schema.o static/data.o static/common.o
libsrepo.a-cflags  := $(common-cflags)
libsrepo.a-pkgconf := libstroll
