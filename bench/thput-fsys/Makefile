# $Id$

top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

CFLAGS = $(COMMON_CFLAGS)
LDLIBS = $(COMMON_LDFLAGS) $(LIBS)

PROGRAM = thput-fsys
OBJS = thput-fsys.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk
