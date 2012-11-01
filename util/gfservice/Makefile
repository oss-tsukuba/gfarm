top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SCRIPTS = gfservice
BUILT_SCRIPTS = gfservice-agent

include $(top_srcdir)/makes/script.mk

gfservice-agent: $(srcdir)/gfservice-agent.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@
