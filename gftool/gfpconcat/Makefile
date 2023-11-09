# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

GFPREP_SRCDIR = $(srcdir)/../gfprep
GFPREP_BUILDDIR = ../gfprep

SRCS = gfpconcat.c pconcat.c \
	$(GFPREP_SRCDIR)/gfmsg.c $(GFPREP_SRCDIR)/gfurl.c \
	$(GFPREP_SRCDIR)/gfurl_local.c $(GFPREP_SRCDIR)/gfurl_gfarm.c \
	$(GFPREP_SRCDIR)/gfurl_hpss.c $(GFPREP_SRCDIR)/gfarm_cmd.c
OBJS = gfpconcat.o pconcat.o \
	$(GFPREP_BUILDDIR)/gfmsg.o $(GFPREP_BUILDDIR)/gfurl.o \
	$(GFPREP_BUILDDIR)/gfurl_local.o $(GFPREP_BUILDDIR)/gfurl_gfarm.o \
	$(GFPREP_BUILDDIR)/gfurl_hpss.o $(GFPREP_BUILDDIR)/gfarm_cmd.o
PROGRAM = gfpconcat
CFLAGS = $(pthread_includes) $(COMMON_CFLAGS) \
	-I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	-I$(GFPREP_SRCDIR) -I$(srcdir) $(openssl_includes)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk
include $(top_srcdir)/makes/subdir.mk

###

$(OBJS): $(DEPGFARMINC) \
	$(GFUTIL_SRCDIR)/gfutil.h \
	$(GFUTIL_SRCDIR)/nanosec.h \
	$(GFARMLIB_SRCDIR)/context.h \
	$(GFARMLIB_SRCDIR)/gfs_pio.h \
	$(GFARMLIB_SRCDIR)/gfarm_path.h \
	$(GFARMLIB_SRCDIR)/lookup.h \
	$(GFPREP_SRCDIR)/gfmsg.h \
	$(GFPREP_SRCDIR)/gfurl.h \
	gfpconcat.h
