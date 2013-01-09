# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

CFLAGS = $(COMMON_CFLAGS)
LDLIBS = $(COMMON_LDFLAGS) $(LIBS)

PROGRAM = bwlat-syscache
OBJS = bwlat-syscache.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk
