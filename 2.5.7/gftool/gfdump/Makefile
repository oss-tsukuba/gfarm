# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

BUILT_SCRIPTS = gfdump.postgresql

include $(top_srcdir)/makes/script.mk

gfdump.postgresql: $(srcdir)/gfdump.postgresql.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@
