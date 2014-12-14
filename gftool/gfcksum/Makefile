# $Id: Makefile 6663 2012-09-21 07:34:19Z tatebe $

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfcksum.c
OBJS = gfcksum.o
PROGRAM = gfcksum
CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	$(openssl_includes)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) $(GFUTIL_SRCDIR)/queue.h $(GFARMLIB_SRCDIR)/gfarm_foreach.h $(GFARMLIB_SRCDIR)/gfarm_foreach.h $(GFARMLIB_SRCDIR)/gfarm_path.h $(GFARMLIB_SRCDIR)/gfs_pio.h
