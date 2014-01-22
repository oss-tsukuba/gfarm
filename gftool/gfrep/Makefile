# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfrep.c gfarm_list.c
OBJS = gfrep.o gfarm_list.o
PROGRAM = gfrep
#CC = omcc
#OMPFLAGS = -fopenmp
CFLAGS = $(OMPFLAGS) $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) $(openssl_includes)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) $(GFUTIL_SRCDIR)/hash.h $(GFARMLIB_SRCDIR)/config.h $(GFARMLIB_SRCDIR)/gfm_client.h $(GFARMLIB_SRCDIR)/gfarm_foreach.h $(GFARMLIB_SRCDIR)/gfarm_path.h gfarm_list.h
