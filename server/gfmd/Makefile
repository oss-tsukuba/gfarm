# $Id$

top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk
include $(srcdir)/../Makefile.inc

CFLAGS = $(COMMON_CFLAGS) $(PTHREAD_CFLAGS) \
	-I$(srcdir) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS) $(PTHREAD_LIBS)
DEPLIBS = $(DEPGFARMLIB)

PROGRAM = gfmd
SRCS =	gfmd.c \
	subr.c id_table.c user.c group.c host.c peer.c process.c job.c \
	inode.c fs.c
OBJS =	gfmd.o \
	subr.o id_table.o user.o group.o host.o peer.o process.o job.o \
	inode.o fs.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) \
	$(GFUTIL_SRCDIR)/gfutil.h \
	$(GFARMLIB_SRCDIR)/gfp_xdr.h \
	$(GFARMLIB_SRCDIR)/io_fd.h \
	$(GFARMLIB_SRCDIR)/sockopt.h \
	$(GFARMLIB_SRCDIR)/auth.h \
	$(GFARMLIB_SRCDIR)/gfm_proto.h \
	$(GFARMLIB_SRCDIR)/gfj_client.h \
	subr.h id_table.h user.h group.h host.h peer.h process.h job.h \
	inode.h fs.h
