# $Id: Makefile 5672 2012-01-10 03:42:04Z n-soda $

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

# $(pthread_includes): ctime_r() needs -D_POSIX_PTHREAD_SEMANTICS on Solaris
CFLAGS = $(pthread_includes) $(COMMON_CFLAGS) \
	-I. -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) -I../include  \
	-DGFARM_DEFAULT_BINDIR=\"$(default_bindir)\"
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

PROGRAM = mount.gfarm
SRCS =	gfskd.c gfskd_proc.c
OBJS =	gfskd.o gfskd_proc.o

all: $(PROGRAM)

install: all
	$(LTINSTALL_PROGRAM) $(PROGRAM) /sbin/$(PROGRAM)

include $(top_srcdir)/makes/prog.mk
include $(top_srcdir)/makes/gflog.mk

###
