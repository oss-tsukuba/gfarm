# $Id$

top_builddir = ../../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

# do not use $(COMMON_CFLAGS) here, to avoid symbol renaming by largefile API.
CFLAGS = -I$(srcdir) -I$(top_srcdir)/lib/gfs_hook -Doff64_t=__off64_t \
	-I$(top_builddir)/include -I$(top_srcdir)/include $(OPTFLAGS)

LIBRARY = libsysdep.la
LIBRARY_RESULT = # do not install

LARGEFILE64_SRCS = getdents64.c xstat64.c lxstat64.c fxstat64.c
LARGEFILE64_OBJS = getdents64.lo xstat64.lo lxstat64.lo fxstat64.lo

SRCS = getdents.c xstat.c lxstat.c fxstat.c xstatconv.c \
	${largefile64_srcs}
OBJS = getdents.lo xstat.lo lxstat.lo fxstat.lo \
	${largefile64_objs}

all:	$(LIBRARY)

include $(top_srcdir)/makes/lib.mk

getdents.lo: getdents.c
	$(LTCOMPILE) -D_LARGEFILE64_SOURCE `getconf LFS64_CFLAGS` -c $(srcdir)/getdents.c

getdents64.lo: getdents64.c getdents.c
	$(LTCOMPILE) -D_LARGEFILE64_SOURCE `getconf LFS64_CFLAGS` -c $(srcdir)/getdents64.c

xstat.lo: xstat.c xstatconv.c

lxstat.lo: lxstat.c xstatconv.c

fxstat.lo: fxstat.c xstatconv.c

xstat64.lo: xstat64.c xstatconv.c
	$(LTCOMPILE) -D_LARGEFILE64_SOURCE `getconf LFS64_CFLAGS` -c $(srcdir)/xstat64.c

lxstat64.lo: lxstat64.c xstatconv.c
	$(LTCOMPILE) -D_LARGEFILE64_SOURCE `getconf LFS64_CFLAGS` -c $(srcdir)/lxstat64.c

fxstat64.lo: fxstat64.c xstatconv.c
	$(LTCOMPILE) -D_LARGEFILE64_SOURCE `getconf LFS64_CFLAGS` -c $(srcdir)/fxstat64.c
