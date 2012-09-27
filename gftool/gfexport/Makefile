# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfexport.c
OBJS = gfexport.o
PROGRAM = gfexport
CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) $(openssl_includes)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) $(GFUTIL_SRCDIR)/queue.h $(GFARMLIB_SRCDIR)/gfs_profile.h $(GFARMLIB_SRCDIR)/host.h $(GFARMLIB_SRCDIR)/config.h $(GFARMLIB_SRCDIR)/gfs_pio.h $(GFARMLIB_SRCDIR)/gfarm_path.h
