# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

PROGRAM = gfdf
SRCS = gfdf_simple.c
OBJS = gfdf_simple.o
CFLAGS = $(COMMON_CFLAGS) -I$(GFARMLIB_SRCDIR) $(openssl_includes)
LDLIBS = $(COMMON_LDLIBS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) \
	$(GFARMLIB_SRCDIR)/config.h \
	$(GFARMLIB_SRCDIR)/gfs_client.h
