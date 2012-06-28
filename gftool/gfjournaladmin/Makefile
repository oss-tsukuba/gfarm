top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SCRIPTS = gfjournaladmin
BUILT_SCRIPTS = gfjournaladmin

include $(top_srcdir)/makes/script.mk

gfjournaladmin: $(srcdir)/gfjournaladmin.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@
