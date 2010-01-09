top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

PROGRAM = gfquota
SRCS = $(PROGRAM).c
OBJS = $(PROGRAM).o
CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) \
	$(GFARMLIB_SRCDIR)/gfm_client.h
