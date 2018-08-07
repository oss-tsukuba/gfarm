# $Id: Makefile 6663 2012-09-21 07:34:19Z tatebe $

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gferror.c
OBJS = gferror.o
PROGRAM = gferror
CFLAGS = $(COMMON_CFLAGS) -I$(GFARMLIB_SRCDIR)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

