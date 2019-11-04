# $Id: Makefile 1265 2004-08-17 01:47:35Z soda $

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

PROGRAM = gfspoolmd5
PROGRAM2 = gfspooldigest
SRCS = $(PROGRAM).c
OBJS = $(PROGRAM).o
CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) $(openssl_includes)
LDLIBS = $(COMMON_LDLIBS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

post-install-hook:
	-rm -f $(DESTDIR)$(bindir)/$(PROGRAM2)
	ln $(DESTDIR)$(bindir)/$(PROGRAM) $(DESTDIR)$(bindir)/$(PROGRAM2)

include $(top_srcdir)/makes/prog.mk
include $(top_srcdir)/makes/gflog.mk

###

$(OBJS): $(DEPGFARMINC) $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/msgdigest.h $(GFARMLIB_SRCDIR)/config.h $(GFARMLIB_SRCDIR)/lookup.h $(GFARMLIB_SRCDIR)/gfm_proto.h $(GFARMLIB_SRCDIR)/gfm_client.h
