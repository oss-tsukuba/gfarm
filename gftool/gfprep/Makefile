# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SUBDIRS = gfpcopy-test

GFREP_SRCDIR = $(srcdir)/../gfrep

SRCS = gfprep.c gfarm_list.c gfarm_parallel.c gfarm_dirtree.c gfarm_fifo.c \
	gfarm_pfunc.c
OBJS = gfprep.o gfarm_list.o gfarm_parallel.o gfarm_dirtree.o gfarm_fifo.o \
	gfarm_pfunc.o
EXTRA_CLEAN_TARGETS = gfarm_list.c
PROGRAM = gfprep
GFPCOPY = gfpcopy
CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	-I$(GFREP_SRCDIR) -I$(srcdir) $(openssl_includes)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

post-install-hook:
	rm -f $(DESTDIR)$(bindir)/$(GFPCOPY)
	ln $(DESTDIR)$(bindir)/$(PROGRAM) $(DESTDIR)$(bindir)/$(GFPCOPY)

gfarm_list.c:
	ln -s $(srcdir)/../gfrep/gfarm_list.c .

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
	gfarm_parallel.h \
	gfarm_dirtree.h \
	gfarm_fifo.h \
	gfarm_pfunc.h
