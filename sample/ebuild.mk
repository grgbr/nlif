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
                       -I $(TOPDIR)/lib

common-ldflags      := $(common-cflags) $(EXTRA_LDFLAGS) \
                       -Wl,--as-needed \
                       -Wl,-z,start-stop-visibility=internal

ifneq ($(filter y,$(CONFIG_NLIF_ASSERT)),)
common-cflags       := $(filter-out -DNDEBUG,$(common-cflags))
common-ldflags      := $(filter-out -DNDEBUG,$(common-ldflags))
endif # ($(filter y,$(CONFIG_NLIF_ASSERT)),)

bins                    += $(call kconf_enabled,NLIF_SAMPLE_SHOW_IFACE, \
                                                nlif-show-iface)
nlif-show-iface-objs    := show_iface.o
nlif-show-iface-lots    := ../lib/builtin.a
nlif-show-iface-cflags  := $(common-cflags)
nlif-show-iface-ldflags := $(common-ldflags) -lynl
nlif-show-iface-pkgconf := libelog libutils libstroll

bins                    += $(call kconf_enabled,NLIF_SAMPLE_SET_IFACE, \
                                                nlif-set-iface)
nlif-set-iface-objs     := set_iface.o
nlif-set-iface-lots     := ../lib/builtin.a
nlif-set-iface-cflags   := $(common-cflags)
nlif-set-iface-ldflags  := $(common-ldflags) -lynl
nlif-set-iface-pkgconf  := libelog libutils libstroll

bins                    += $(call kconf_enabled,NLIF_SAMPLE_MON,nlif-mon)
nlif-mon-objs           := mon.o
nlif-mon-lots           := ../lib/builtin.a
nlif-mon-cflags         := $(common-cflags)
nlif-mon-ldflags        := $(common-ldflags) -lynl
nlif-mon-pkgconf        := libelog libutils libstroll

# ex: filetype=make :
