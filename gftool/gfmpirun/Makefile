# $Id$

top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfmpirun.c
OBJS = gfmpirun.o
PROGRAM = gfmpirun
CFLAGS = $(COMMON_CFLAGS) -I$(GFARMLIB_SRCDIR)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC)
$(OBJS): $(DEPGFARMINC) \
	$(GFARMLIB_SRCDIR)/host.h \
	$(GFARMLIB_SRCDIR)/gfj_client.h
