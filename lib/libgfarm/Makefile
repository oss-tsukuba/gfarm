# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

# shared object version
LTLDFLAGS = $(LTFLAGS_SHARELIB_IN) $(libdir)
LT_CURRENT=1
LT_REVISION=0
LT_AGE=0

LIBRARY = libgfarm.la
CFLAGS = $(COMMON_CFLAGS)
LDLIBS = $(globus_gssapi_libs) $(openssl_libs) $(LIBS)

GLOBUS_TARGETS = gfsl
SUBDIRS = gfutil $(globus_targets) gfarm 

GLOBUS_OBJS = gfsl/libgfsl.la
OBJS =	gfutil/libgfutil.la $(globus_objs) gfarm/libgfarmcore.la

all: subdir-all $(LIBRARY)

$(OBJS): subdir-all

include $(top_srcdir)/makes/subdir.mk
include $(top_srcdir)/makes/lib.mk

clean:
	$(RM) -f hook_test hook_test_mpi
