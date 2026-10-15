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
                       -fvisibility=internal \
                       -I ../lib

common-ldflags      := $(common-cflags) $(EXTRA_LDFLAGS) \
                       -Wl,--as-needed \
                       -Wl,-z,start-stop-visibility=internal

ifneq ($(filter y,$(CONFIG_NLIF_ASSERT)),)
common-cflags       := $(filter-out -DNDEBUG,$(common-cflags))
common-ldflags      := $(filter-out -DNDEBUG,$(common-ldflags))
endif # ($(filter y,$(CONFIG_NLIF_ASSERT)),)

bins                += $(call kconf_enabled,NLIF_DAEMON,nlifd)
nlifd-objs          := nlifd.o
nlifd-lots          := ../lib/builtin.a
nlifd-cflags        := $(common-cflags)
nlifd-ldflags       := $(common-ldflags) -lynl
nlifd-pkgconf       += $(call kconf_enabled,NLIF_LOG,libelog)
nlifd-pkgconf       += libutils libstroll
nlifd-path          := $(SBINDIR)/nlifd

# ex: filetype=make :
