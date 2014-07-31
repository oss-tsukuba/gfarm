# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk
include $(srcdir)/../Makefile.inc

# $(pthread_includes): ctime_r() needs -D_POSIX_PTHREAD_SEMANTICS on Solaris
CFLAGS = $(pthread_includes) $(COMMON_CFLAGS) \
	-I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) $(openssl_includes) \
	-DGFARM_DEFAULT_BINDIR=\"$(default_bindir)\"
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

PROGRAM = gfsd
SRCS =	gfsd.c loadavg.c statfs.c spck.c
OBJS =	gfsd.o loadavg.o statfs.o spck.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk
include $(top_srcdir)/makes/gflog.mk

###

$(OBJS): $(DEPGFARMINC) \
	$(GFUTIL_SRCDIR)/gfutil.h \
	$(GFUTIL_SRCDIR)/gflog_reduced.h \
	$(GFUTIL_SRCDIR)/msgdigest.h \
	$(GFUTIL_SRCDIR)/nanosec.h \
	$(GFUTIL_SRCDIR)/hash.h \
	$(GFUTIL_SRCDIR)/timer.h \
	$(GFARMLIB_SRCDIR)/context.h \
	$(GFARMLIB_SRCDIR)/gfp_xdr.h \
	$(GFARMLIB_SRCDIR)/io_fd.h \
	$(GFARMLIB_SRCDIR)/sockopt.h \
	$(GFARMLIB_SRCDIR)/hostspec.h \
	$(GFARMLIB_SRCDIR)/host.h \
	$(GFARMLIB_SRCDIR)/conn_hash.h \
	$(GFARMLIB_SRCDIR)/auth.h \
	$(GFARMLIB_SRCDIR)/config.h \
	$(GFARMLIB_SRCDIR)/gfs_proto.h \
	$(GFARMLIB_SRCDIR)/gfs_client.h \
	$(GFARMLIB_SRCDIR)/gfm_proto.h \
	$(GFARMLIB_SRCDIR)/gfm_client.h \
	$(GFARMLIB_SRCDIR)/gfs_profile.h \
	$(srcdir)/gfsd_subr.h
