# $Id: Makefile 6684 2012-09-27 15:30:59Z tatebe $

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfrepcheck.c
OBJS = gfrepcheck.o
PROGRAM = gfrepcheck
CFLAGS = $(COMMON_CFLAGS) -I$(GFARMLIB_SRCDIR)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) $(GFARMLIB_SRCDIR)/gfm_client.h $(GFARMLIB_SRCDIR)/lookup.h $(GFARMLIB_SRCDIR)/gfarm_path.h
