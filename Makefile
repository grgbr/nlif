OUTDIR      := $(CURDIR)/out
BUILDDIR    := $(OUTDIR)/build
SRCDIR      := $(CURDIR)/src
STAGING     := $(OUTDIR)/staging
DESTDIR     :=

RSYNC       := rsync
INSTALL     := install

LINUXDIR    := $(CURDIR)/external/linux-7.1.9
IPROUTE2DIR := $(CURDIR)/external/iproute2-7.1.0
YNLDIR      := $(LINUXDIR)/tools/net/ynl/ynl-gen-c.py
YNLCLI      := $(YNLDIR)/cli.py
YNLGEN      := $(YNLDIR)/ynl-gen-c.py
CSCOPE      := cscope
CTAGS       := ctags

CFLAGS      := -Wall -Wextra -O0 -ggdb3 -I$(SRCDIR) -I$(STAGING)/include -I../out/root/include
LDFLAGS     := -L$(BUILDDIR) -L$(STAGING)/lib64 -L../out/root/lib -Wl,-rpath,$(realpath ../out/root/lib)

build: $(BUILDDIR)/show $(BUILDDIR)/mon $(BUILDDIR)/nlifd

# TODO: make compilation dependent on CONFIG_NLIF_OBSRV and -lutils
$(BUILDDIR)/nlifd: src/nlifd.c $(BUILDDIR)/libnlif.a
	$(CC) -MD -o $(@) $(CFLAGS) $(<) $(LDFLAGS) -lnlif -lynl -lelog -lutils -lstroll
# TODO: make compilation dependent on CONFIG_NLIF_OBSRV and -lutils
$(BUILDDIR)/mon: sample/mon.c $(BUILDDIR)/libnlif.a
	$(CC) -MD -o $(@) $(CFLAGS) $(<) $(LDFLAGS) -lnlif -lynl -lelog -lutils -lstroll
# TODO: make compilation dependent on CONFIG_NLIF_OBSRV and -lutils
$(BUILDDIR)/show: sample/show.c $(BUILDDIR)/libnlif.a
	$(CC) -MD -o $(@) $(CFLAGS) $(<) $(LDFLAGS) -lnlif -lynl -lelog -lutils -lstroll
# TODO: make compilation of obsrv.o dependent on CONFIG_NLIF_OBSRV
$(BUILDDIR)/libnlif.a: $(addprefix $(BUILDDIR)/,store.o iface.o gate.o link.o obsrv.o common.o)
	$(AR) crs $(@) $(^)
$(BUILDDIR)/%.o: $(SRCDIR)/%.c Makefile
	$(CC) -MD -o $(@) $(CFLAGS) -c $(<)

-include $(BUILDDIR)/*.d

config-linux: $(BUILDDIR)/linux/
	$(MAKE) --directory="$(LINUXDIR)" \
		O="$(BUILDDIR)/linux" \
		defconfig

build-linux: config-linux
	$(MAKE) --directory="$(LINUXDIR)" \
		O="$(BUILDDIR)/linux" \
		headers

clean-linux:
	$(RM) -r $(BUILDDIR)/linux

install-linux: build-linux
	$(MAKE) --directory="$(LINUXDIR)" \
		O="$(BUILDDIR)/linux" \
		INSTALL_HDR_PATH="$(STAGING)" \
		headers_install

build-ynl: | $(BUILDDIR)/ynl/
	$(MAKE) --directory="$(LINUXDIR)/tools" \
		DESTDIR="$(DESTDIR)" \
		prefix="$(STAGING)" \
		bindir="$(STAGING)/bin" \
		O="$(BUILDDIR)/ynl" \
		ynl #DEBUG=1

clean-ynl: | $(BUILDDIR)/ynl/
	$(MAKE) --directory="$(LINUXDIR)/tools" \
		DESTDIR="$(DESTDIR)" \
		prefix="$(STAGING)" \
		bindir="$(STAGING)/bin" \
		O="$(BUILDDIR)/ynl" \
		ynl_clean

install-ynl: build-ynl | $(STAGING)/bin/ $(STAGING)/lib/
	$(MAKE) --directory="$(LINUXDIR)/tools" \
		DESTDIR="$(DESTDIR)" \
		prefix="$(STAGING)" \
		bindir="$(STAGING)/bin" \
		O="$(BUILDDIR)/ynl" \
		ynl_install #DEBUG=1
	$(RSYNC) -a $(DESTDIR)$(STAGING)/local/bin/ $(DESTDIR)$(STAGING)/bin
	$(RSYNC) -a $(DESTDIR)$(STAGING)/local/lib/ $(DESTDIR)$(STAGING)/lib
	$(RM) -r $(DESTDIR)$(STAGING)/local
	$(INSTALL) --mode=755 scripts/ynl.py $(DESTDIR)$(STAGING)/bin/ynl

$(BUILDDIR)/linux/tags: | build-ynl config-linux
	$(MAKE) --directory="$(LINUXDIR)" \
	        O="$(BUILDDIR)/linux" \
	        tags

$(BUILDDIR)/linux/cscope.out: | build-ynl config-linux
	$(MAKE) --directory="$(LINUXDIR)" \
	        O="$(BUILDDIR)/linux" \
	        cscope

dev-linux: $(BUILDDIR)/linux/cscope.out $(BUILDDIR)/linux/tags

dev: | $(BUILDDIR)/src/ dev-linux
	$(CTAGS) --recurse=yes -f $(BUILDDIR)/src/tags $(SRCDIR)
	$(CSCOPE) -b -q -f$(BUILDDIR)/src/cscope.out -s$(SRCDIR)

$(BUILDDIR)/ynl/ \
$(BUILDDIR)/linux/ \
$(STAGING)/bin/ \
$(STAGING)/lib/ \
$(BUILDDIR)/src/:
	mkdir -p $(@)
