# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SUBDIRS = gfpcopy-test gfpcopy-stress

GFREP_SRCDIR = $(srcdir)/../gfrep
GFREP_BUILDDIR = ../gfrep

SRCS = gfprep.c gfarm_parallel.c gfarm_dirtree.c gfarm_fifo.c gfarm_pfunc.c \
	gfmsg.c gfurl.c gfurl_local.c gfurl_gfarm.c gfurl_hpss.c gfarm_cmd.c \
	$(GFREP_SRCDIR)/gfarm_list.c
OBJS = gfprep.o gfarm_parallel.o gfarm_dirtree.o gfarm_fifo.o gfarm_pfunc.o \
	gfmsg.o gfurl.o gfurl_local.o gfurl_gfarm.o gfurl_hpss.o gfarm_cmd.o \
	$(GFREP_BUILDDIR)/gfarm_list.o
EXTRA_CLEAN_TARGETS = gfarm_list.c
PROGRAM = gfprep
GFPCOPY = gfpcopy
CFLAGS = $(pthread_includes) $(COMMON_CFLAGS) \
	-I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	-I$(GFREP_SRCDIR) -I$(srcdir) $(openssl_includes)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

post-install-hook:
	rm -f $(DESTDIR)$(bindir)/$(GFPCOPY)
	ln $(DESTDIR)$(bindir)/$(PROGRAM) $(DESTDIR)$(bindir)/$(GFPCOPY)

include $(top_srcdir)/makes/prog.mk
include $(top_srcdir)/makes/subdir.mk

###

$(OBJS): $(DEPGFARMINC) \
	$(GFUTIL_SRCDIR)/hash.h \
	$(GFUTIL_SRCDIR)/nanosec.h \
	$(GFUTIL_SRCDIR)/thrsubr.h \
	$(GFUTIL_SRCDIR)/gfutil.h \
	$(GFUTIL_SRCDIR)/queue.h \
	$(GFARMLIB_SRCDIR)/config.h \
	$(GFARMLIB_SRCDIR)/context.h \
	$(GFARMLIB_SRCDIR)/gfarm_path.h \
	$(GFARMLIB_SRCDIR)/gfm_client.h \
	$(GFARMLIB_SRCDIR)/host.h \
	$(GFARMLIB_SRCDIR)/lookup.h \
	$(GFARMLIB_SRCDIR)/gfs_pio.h \
	$(GFARMLIB_SRCDIR)/humanize_number.h \
	$(GFREP_SRCDIR)/gfarm_list.h \
	gfprep.h \
	gfmsg.h \
	gfurl.h \
	gfarm_cmd.h \
	gfarm_parallel.h \
	gfarm_dirtree.h \
	gfarm_fifo.h \
	gfarm_pfunc.h
