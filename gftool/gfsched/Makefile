# $Id$

top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfsched.c
OBJS = gfsched.o
PROGRAM = gfsched
CFLAGS = -I$(top_objdir) -I$(INCDIR) $(OPTFLAGS)
LDLIBS = $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC)
