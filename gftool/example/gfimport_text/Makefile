# $Id$

top_objdir = ../../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk
include $(srcdir)/../Makefile.inc

SRCS = gfimport_text.c
OBJS = gfimport_text.o
PROGRAM = gfimport_text
CFLAGS = -I$(top_objdir) -I$(INCDIR) $(OPTFLAGS)
LDLIBS = $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC)
