# $Id: Makefile 1265 2004-08-17 01:47:35Z soda $

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

PROGRAM = gfspoolmd5
SRCS = $(PROGRAM).c
OBJS = $(PROGRAM).o
CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR)
LDLIBS = $(COMMON_LDLIBS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/md5.h $(GFARMLIB_SRCDIR)/lookup.h $(GFARMLIB_SRCDIR)/gfm_proto.h $(GFARMLIB_SRCDIR)/gfm_client.h
