# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfrm_simple.c
OBJS = gfrm_simple.o
PROGRAM = gfrm
#CC = omcc
#OMPFLAGS = -fopenmp
CFLAGS = $(OMPFLAGS) $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) $(openssl_includes)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) $(GFUTIL_SRCDIR)/hash.h $(GFARMLIB_SRCDIR)/host.h $(GFARMLIB_SRCDIR)/schedule.h $(GFARMLIB_SRCDIR)/gfs_client.h  $(GFARMLIB_SRCDIR)/gfs_misc.h $(GFARMLIB_SRCDIR)/gfarm_path.h
