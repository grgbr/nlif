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
                       -fvisibility=internal

common-ldflags      := $(common-cflags) $(EXTRA_LDFLAGS) \
                       -Wl,--as-needed \
                       -Wl,-z,start-stop-visibility=internal

ifneq ($(filter y,$(CONFIG_NLIF_ASSERT)),)
common-cflags       := $(filter-out -DNDEBUG,$(common-cflags))
common-ldflags      := $(filter-out -DNDEBUG,$(common-ldflags))
endif # ($(filter y,$(CONFIG_NLIF_ASSERT)),)

builtins            := builtin.a
builtin.a-objs      := common.o
builtin.a-objs      += gate.o
builtin.a-objs      += iface.o
builtin.a-objs      += link.o
builtin.a-objs      += $(call kconf_enabled,NLIF_NOTIF,obsrv.o)
builtin.a-objs      += $(call kconf_enabled,NLIF_STORE,store.o)
builtin.a-cflags    := $(common-cflags)
builtin.a-pkgconf   += $(call kconf_enabled,NLIF_LOG,libelog)
builtin.a-pkgconf   += libstroll

# ex: filetype=make :
