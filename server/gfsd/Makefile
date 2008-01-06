# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk
include $(srcdir)/../Makefile.inc

CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	$(openssl_includes) \
	-DGFARM_DEFAULT_BINDIR=\"$(default_bindir)\"
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

PROGRAM = gfsd
SRCS =	gfsd.c loadavg.c statfs.c spck.c
OBJS =	gfsd.o loadavg.o statfs.o spck.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) \
	$(srcdir)/gfsd_subr.h \
	$(GFUTIL_SRCDIR)/gfutil.h \
	$(GFARMLIB_SRCDIR)/iobuffer.h \
	$(GFARMLIB_SRCDIR)/io_fd.h \
	$(GFARMLIB_SRCDIR)/param.h \
	$(GFARMLIB_SRCDIR)/sockopt.h \
	$(GFARMLIB_SRCDIR)/host.h \
	$(GFARMLIB_SRCDIR)/auth.h \
	$(GFARMLIB_SRCDIR)/config.h \
	$(GFARMLIB_SRCDIR)/gfs_proto.h \
	$(GFARMLIB_SRCDIR)/gfs_client.h
