# $Id: Makefile 2533 2006-03-31 10:50:56Z takuya $

top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	-I$(srcdir)/../gfperf-lib $(openssl_includes)
LDLIBS = $(COMMON_LDFLAGS) -L../gfperf-lib $(GFARMLIB) $(LIBS) -lgfperf

PROGRAM = gfperf-copy
OBJS = gfperf-copy-main.o gfperf-copy-libgfarm.o gfperf-copy-gfarm2fs.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) gfperf-copy.h $(srcdir)/../gfperf-lib/gfperf-lib.h $(GFARMLIB_SRCDIR)/gfs_pio.h $(GFARMLIB_SRCDIR)/host.h $(GFUTIL_SRCDIR)/queue.h
