# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

VPATH=$(GFUTIL_SRCDIR):$(GFSL_SRCDIR):$(GFARMLIB_SRCDIR)

LIB = gfarm

CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	$(globus_cflags) $(metadb_client_includes) $(openssl_includes)

all: lib$(LIB).a

include $(srcdir)/gfutil/Makefile.inc
include $(srcdir)/gfsl/Makefile.inc
include $(srcdir)/gfarm/Makefile.inc

GLOBUS_CFLAGS = -I$(GFSL_SRCDIR) $(globus_includes) \
	-DGLOBUS_BUG=1 # see gfsl/gsi.c
GLOBUS_SRCS = $(GFSL_SRCS) $(GFARM_GLOBUS_SRCS)
GLOBUS_OBJS = $(GFSL_OBJS) $(GFARM_GLOBUS_OBJS)

SRCS =	$(GFUTIL_SRCS) $(GFARM_SRCS) $(globus_srcs)
OBJS =	$(GFUTIL_OBJS) $(GFARM_OBJS) $(globus_objs)

lib$(LIB).a: $(OBJS)
	ar rucv lib$(LIB).a $(OBJS)
	$(RANLIB) lib$(LIB).a

install: all
	$(INSTALL_DATA) lib$(LIB).a $(libdir)/lib$(LIB).a

clean:; rm -f *.o

veryclean: clean
	rm -f lib$(LIB).a

distclean: veryclean

gfregister:
man:
html:
