# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk
include $(srcdir)/../Makefile.inc

CFLAGS = $(pthread_includes) \
	$(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS) $(pthread_libs)
DEPLIBS = $(DEPGFARMLIB)

PROGRAM = gfmd
SRCS =	gfmd.c \
	subr.c id_table.c user.c group.c host.c peer.c process.c job.c \
	dir.c inode.c fs.c metadb_if.c
OBJS =	gfmd.o \
	subr.o id_table.o user.o group.o host.o peer.o process.o job.o \
	dir.o inode.o fs.o metadb_if.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) \
	$(GFUTIL_SRCDIR)/gfutil.h \
	$(GFUTIL_SRCDIR)/hash.h \
	$(GFUTIL_SRCDIR)/tree.h \
	$(GFARMLIB_SRCDIR)/gfp_xdr.h \
	$(GFARMLIB_SRCDIR)/io_fd.h \
	$(GFARMLIB_SRCDIR)/sockopt.h \
	$(GFARMLIB_SRCDIR)/auth.h \
	$(GFARMLIB_SRCDIR)/config.h \
	$(GFARMLIB_SRCDIR)/gfm_proto.h \
	$(GFARMLIB_SRCDIR)/gfj_client.h \
	subr.h id_table.h user.h group.h host.h peer.h process.h job.h \
	dir.h inode.h fs.h
