# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

DOCBOOK_DIR=$(top_srcdir)/doc/docbook/en/ref/man1
include $(DOCBOOK_DIR)/Makefile.inc
MAN = $(DOCBOOK) #gfrsh.1 gfssh.1
EXTRA_CLEAN_TARGETS = manpage.links manpage.refs

include $(top_srcdir)/makes/man.mk
