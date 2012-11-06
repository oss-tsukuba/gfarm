# $Id: Makefile 5386 2011-06-21 06:07:22Z n-soda $

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk
#include $(srcdir)/../Makefile.inc

CFLAGS = $(COMMON_CFLAGS) -I../include -I/usr/local/include
LDLIBS = -levent
DEPLIBS = 

PROGRAM = ug_idmapd

SRCS =	idmapd.c
OBJS =	idmapd.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk
include $(top_srcdir)/makes/gflog.mk

###

$(OBJS): queue.h $(SRCS)
include $(optional_rule)
