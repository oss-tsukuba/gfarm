top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

BUILT_SCRIPTS = gfarmbb

include $(top_srcdir)/makes/script.mk

gfarmbb: $(srcdir)/gfarmbb.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@
