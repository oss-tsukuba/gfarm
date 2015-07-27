# $Id: Makefile 8426 2013-07-29 01:05:54Z m-kasahr $

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

DOCBOOK_DIR=$(top_srcdir)/doc/docbook/en/ref/man7
include $(DOCBOOK_DIR)/Makefile.inc
MAN = $(DOCBOOK)
DOCBOOK2MAN_XSL = ../docbook2man.xsl
EXTRA_CLEAN_TARGETS = manpage.links manpage.refs

include $(top_srcdir)/makes/man.mk
