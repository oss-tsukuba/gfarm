# $Id$

top_builddir = ../../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

CFLAGS = -I$(srcdir) -D_ASM

LIBRARY = libsysdep.la
LIBRARY_RESULT = # do not install

SRCS = llseek.s
OBJS = llseek.lo

all:	$(LIBRARY)

include $(top_srcdir)/makes/lib.mk
