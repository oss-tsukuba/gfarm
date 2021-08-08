# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

GFPREP_SRCDIR = $(srcdir)/../gfprep

SRCS = gfpcat.c gfmsg.c gfurl.c gfurl_local.c gfurl_gfarm.c gfurl_hpss.c \
	gfarm_cmd.c
OBJS = gfpcat.o gfmsg.o gfurl.o gfurl_local.o gfurl_gfarm.o gfurl_hpss.o \
	gfarm_cmd.o
EXTRA_CLEAN_TARGETS = gfmsg.c gfurl.c gfurl_local.c gfurl_gfarm.c
PROGRAM = gfpcat
GFPCOPY = gfpcopy
CFLAGS = $(pthread_includes) $(COMMON_CFLAGS) \
	-I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	-I$(GFPREP_SRCDIR) -I$(srcdir) $(openssl_includes)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

gfmsg.c:
	ln -s $(GFPREP_SRCDIR)/$@ .

gfurl.c:
	ln -s $(GFPREP_SRCDIR)/$@ .

gfurl_local.c:
	ln -s $(GFPREP_SRCDIR)/$@ .

gfurl_gfarm.c:
	ln -s $(GFPREP_SRCDIR)/$@ .

gfurl_hpss.c:
	ln -s $(GFPREP_SRCDIR)/$@ .

gfarm_cmd.c:
	ln -s $(GFPREP_SRCDIR)/$@ .

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
	$(GFPREP_SRCDIR)/gfurl.h
