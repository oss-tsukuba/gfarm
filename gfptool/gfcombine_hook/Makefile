# $Id$

top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfcombine_hook.c
OBJS = gfcombine_hook.o
PROGRAM = gfcombine_hook
CFLAGS = $(COMMON_CFLAGS)
LDLIBS = $(COMMON_LDFLAGS) $(top_objdir)/lib/gfs_hook/gfs_hook.o $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB) $(top_objdir)/lib/gfs_hook/gfs_hook.o

all: $(PROGRAM)

include $(top_srcdir)/makes/gfptool.mk

###

$(OBJS): $(DEPGFARMINC)
