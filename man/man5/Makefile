# $Id$

top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

DOCBOOK_DIR=$(top_srcdir)/doc/docbook/en/ref/man5
include $(DOCBOOK_DIR)/Makefile.inc
MAN = $(DOCBOOK)
EXTRA_CLEAN_TARGETS = manpage.links manpage.refs

include $(top_srcdir)/makes/man.mk
