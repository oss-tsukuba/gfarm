# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfrun.c
OBJS = gfrun.o
PROGRAM = gfrun

PROGRAM_LINKS = gfrsh gfssh gfrshl gfsshl
EXTRA_VERYCLEAN_TARGETS = $(PROGRAM_LINKS)

CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM) $(PROGRAM_LINKS)

include $(top_srcdir)/makes/prog.mk

$(PROGRAM_LINKS): $(PROGRAM)
	rm -f $@
	ln $(PROGRAM) $@

post-install-hook:
	for f in $(PROGRAM_LINKS); do \
		rm -f $(DESTDIR)$(bindir)/$$f; \
		ln $(DESTDIR)$(bindir)/$(PROGRAM) $(DESTDIR)$(bindir)/$$f; \
	done

###

$(OBJS): $(DEPGFARMINC) \
	$(GFUTIL_SRCDIR)/gfutil.h \
	$(GFARMLIB_SRCDIR)/host.h \
	$(GFARMLIB_SRCDIR)/config.h \
	$(GFARMLIB_SRCDIR)/gfj_client.h \
	$(GFARMLIB_SRCDIR)/schedule.h
