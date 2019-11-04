# $Id: Makefile 3160 2006-08-30 16:53:21Z tatebe $

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gftest.c
OBJS = gftest.o
PROGRAM = gftest
CFLAGS = $(COMMON_CFLAGS) -I$(GFARMLIB_SRCDIR)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) $(GFARMLIB_SRCDIR)/gfs_misc.h
