# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

PROGRAM = gfchown
PROGRAM2 = gfchgrp
SRCS = $(PROGRAM).c
OBJS = $(PROGRAM).o
CFLAGS = $(COMMON_CFLAGS)
LDLIBS = $(COMMON_LDLIBS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

post-install-hook:
	-rm -f $(DESTDIR)$(bindir)/$(PROGRAM2)
	ln $(DESTDIR)$(bindir)/$(PROGRAM) $(DESTDIR)$(bindir)/$(PROGRAM2)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC)
