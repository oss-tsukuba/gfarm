# $Id$

top_objdir = ../../../../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

htmldir=$(default_htmldir)/en/ref/man1

DOCBOOK_DIR=$(top_srcdir)/doc/docbook/en/ref/man1
include $(DOCBOOK_DIR)/Makefile.inc
HTMLSRC=$(DOCBOOK)

include $(top_srcdir)/makes/html.mk
