# $Id: Makefile 2612 2006-05-29 04:24:37Z soda $

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

PROGRAM = gfln
SRCS = $(PROGRAM).c
OBJS = $(PROGRAM).o
CFLAGS = $(COMMON_CFLAGS) -I$(GFARMLIB_SRCDIR)
LDLIBS = $(COMMON_LDLIBS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) $(GFARMLIB_SRCDIR)/gfarm_path.h
