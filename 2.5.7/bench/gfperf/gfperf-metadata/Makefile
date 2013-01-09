# $Id: Makefile 2533 2006-03-31 10:50:56Z takuya $

top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

CFLAGS = $(COMMON_CFLAGS) -I$(srcdir)/../gfperf-lib
LDLIBS = $(COMMON_LDFLAGS) -L../gfperf-lib $(GFARMLIB) $(LIBS) -lgfperf

PROGRAM = gfperf-metadata
OBJS = gfperf-metadata-main.o gfperf-metadata-libgfarm.o \
       gfperf-metadata-gfarm2fs.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk
