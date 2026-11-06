top_builddir = ../../../../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

PROGRAM = search_idle_race
SRCS = $(PROGRAM).c
OBJS = $(PROGRAM).o
CFLAGS = $(pthread_includes) $(COMMON_CFLAGS) \
	-I$(GFARMLIB_SRCDIR) -I$(GFUTIL_SRCDIR)
LDLIBS = $(COMMON_LDLIBS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) $(GFARMLIB_SRCDIR)/gfm_client.h \
	$(GFARMLIB_SRCDIR)/gfs_client.h $(GFARMLIB_SRCDIR)/lookup.h \
	$(GFARMLIB_SRCDIR)/schedule.h
