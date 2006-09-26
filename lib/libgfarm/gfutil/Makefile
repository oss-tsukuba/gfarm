# $Id$

top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

LIBRARY = libgfutil.la
LIBRARY_RESULT = # do not install

SRCS =	alloc.c \
	daemon.c \
	gfevent.c \
	hash.c \
	limit.c \
	logutil.c \
	logutil_th.c \
	send_no_sigpipe.c \
	timer.c \
	timeval.c
OBJS =	alloc.lo \
	daemon.lo \
	gfevent.lo \
	hash.lo \
	limit.lo \
	logutil.lo \
	logutil_th.lo \
	send_no_sigpipe.lo \
	timer.lo \
	timeval.lo

CFLAGS = $(pthread_includes) $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR)

all: $(LIBRARY)

include $(top_srcdir)/makes/lib.mk

###

$(OBJS): $(DEPGFARMINC)
daemon.lo: gfutil.h
gfevent.lo: $(INC_SRCDIR)/gfarm_misc.h gfutil.h gfevent.h
hash.lo: gfutil.h hash.h
limit.lo: gfutil.h
logutil.lo: gfutil.h logutil.h
logutil_th.lo: $(INC_SRCDIR)/gfarm_misc.h logutil.h
timer.lo: timer.h
timeval.lo: gfutil.h
