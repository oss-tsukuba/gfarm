# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfreg_simple.c
OBJS = gfreg_simple.o
PROGRAM = gfreg
CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	$(openssl_includes)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/gfptool.mk

###

$(OBJS): $(DEPGFARMINC) $(GFUTIL_SRCDIR)/queue.h $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/timer.h $(GFARMLIB_SRCDIR)/context.h $(GFARMLIB_SRCDIR)/gfs_profile.h $(GFARMLIB_SRCDIR)/host.h $(GFARMLIB_SRCDIR)/gfs_pio.h $(GFARMLIB_SRCDIR)/gfarm_path.h
