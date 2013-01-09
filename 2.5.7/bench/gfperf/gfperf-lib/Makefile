# $Id: Makefile 4583 2010-03-26 17:34:15Z tatebe $

top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

# shared object version
LTLDFLAGS = $(LTFLAGS_SHARELIB_IN) $(libdir)
LT_CURRENT=1
LT_REVISION=0
LT_AGE=0

LIBRARY = libgfperf.la

CFLAGS = $(pthread_includes) \
	$(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) -I$(srcdir) \
	$(globus_cflags) $(metadb_client_includes) $(openssl_includes) \
	$(optional_cflags) \
	-DGFARM_CONFIG='"$(sysconfdir)/gfarm2.conf"'

SRCS = gfperf-util.c create_file.c
OBJS = gfperf-util.lo create_file.lo

all: $(LIBRARY)

include $(top_srcdir)/makes/lib.mk
include $(top_srcdir)/makes/gflog.mk

util.lo: gfperf-lib.h
