# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

CFLAGS = $(COMMON_CFLAGS) -I/usr/local/include -DPVFS_MOUNT=\"/mnt/pvfs\"
LDLIBS = $(COMMON_LDFLAGS) $(LIBS) -L/usr/local/lib -lpvfs

PROGRAM = thput-pvfs
OBJS = thput-pvfs.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk
