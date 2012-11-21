top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

BUILT_SCRIPTS = gfruntest

include $(top_srcdir)/makes/script.mk

post-install-hook:
	@$(MKDIR_P) $(DESTDIR)$(datadir)/gfarm/systest/testcases
	@$(MKDIR_P) $(DESTDIR)$(datadir)/gfarm/systest/parameters
	@$(MKDIR_P) $(DESTDIR)$(datadir)/gfarm/systest/common_scripts
gfruntest: $(srcdir)/gfruntest.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@
