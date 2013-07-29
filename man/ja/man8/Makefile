# $Id$

top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk
include $(srcdir)/../Makefile.inc

DOCBOOK_DIR=$(top_srcdir)/doc/docbook/ja/ref/man8
include $(DOCBOOK_DIR)/Makefile.inc
MAN = $(DOCBOOK)
DOCBOOK2MAN_XSL = ../docbook2man.xsl
EXTRA_CLEAN_TARGETS = manpage.links manpage.refs

include $(top_srcdir)/makes/man.mk
