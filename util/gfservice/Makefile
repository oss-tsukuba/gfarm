top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

BUILT_SCRIPTS = gfservice gfservice-agent gfservice-timeout

include $(top_srcdir)/makes/script.mk

post-install-hook:
	@$(MKDIR_P) $(DESTDIR)$(datadir)/gfarm/gfservice
gfservice: $(srcdir)/gfservice.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@
gfservice-agent: $(srcdir)/gfservice-agent.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@
gfservice-timeout: $(srcdir)/gfservice-timeout.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@
