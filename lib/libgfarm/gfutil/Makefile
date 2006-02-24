# $Id$

top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

LIBRARY = libgfutil.la
LIBRARY_RESULT = # do not install

SRCS = daemon.c gfevent.c hash.c limit.c logutil.c logutil_th.c timer.c timeval.c
OBJS = daemon.lo gfevent.lo hash.lo limit.lo logutil.lo logutil_th.lo timer.lo timeval.lo

CFLAGS = $(pthread_includes) $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR)

all: $(LIBRARY)

include $(top_srcdir)/makes/lib.mk

###

$(OBJS): $(DEPGFARMINC)
daemon.lo: gfutil.h
gfevent.lo: gfutil.h gfevent.h
hash.lo: hash.h
limit.lo: gfutil.h
logutil.lo: gfutil.h logutil.h
logutil_th.lo: logutil.h
timer.lo: timer.h
timeval.lo: gfutil.h
