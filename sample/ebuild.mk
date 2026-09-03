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

bins                += $(call kconf_enabled,NLIF_SAMPLE_SHOW,nlif-show)
nlif-show-objs      := show.o
nlif-show-lots      := ../lib/builtin.a
nlif-show-cflags    := $(common-cflags)
nlif-show-ldflags   := $(common-ldflags) -lynl
nlif-show-pkgconf   := libelog libstroll

bins                += $(call kconf_enabled,NLIF_SAMPLE_MON,nlif-mon)
nlif-mon-objs       := mon.o
nlif-mon-lots       := ../lib/builtin.a
nlif-mon-cflags     := $(common-cflags)
nlif-mon-ldflags    := $(common-ldflags) -lynl
nlif-mon-pkgconf    := libelog libutils libstroll

# ex: filetype=make :
