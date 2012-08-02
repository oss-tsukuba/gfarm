# $Id: Makefile 5520 2011-09-26 07:40:38Z m-kasahr $

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

datadir=$(default_datadir)/gfarm/config

BUILT_SCRIPTS = gfjournaldump

include $(top_srcdir)/makes/script.mk

gfjournaldump: $(srcdir)/gfjournaldump.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@
