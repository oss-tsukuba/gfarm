top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

# gfregister is an administration command.
bindir = $(sbindir)

SCRIPTS= gfregister

include $(top_srcdir)/makes/script.mk

distclean:
	-rm -f $(SCRIPTS)
