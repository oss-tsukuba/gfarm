top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

datadir = $(default_datadir)/gfarm/voms
confpath = $(sysconfdir)

vomsids_name = gfarm.vomsids
hostidpy_name = hostid.py

DATA = $(srcdir)/zsi_def.py $(hostidpy_name)

all: $(vomsids_name)

$(vomsids_name):
	./mkscript $(hostidpy_name) $(confpath) $(vomsids_name)

post-install-hook:
	@if ! test -e "$(confpath)/`basename $(vomsids_name)`"; then \
	echo \
	$(INSTALL_DATA) $(srcdir)/$(vomsids_name) $(DESTDIR)$(confpath)/`basename $(vomsids_name)`; \
	$(INSTALL_DATA) $(srcdir)/$(vomsids_name) $(DESTDIR)$(confpath)/`basename $(vomsids_name)`; \
	fi

post-clean-hook:
	@for i in -- $(vomsids_name) $(hostidpy_name); do \
		test ! -e $$i || $(RM) -f $$i; \
	done

include $(top_srcdir)/makes/data.mk
