top_builddir = ../../../..
top_srcdir = $(top_builddir)
srcdir =.

include $(top_srcdir)/makes/var.mk
include $(top_srcdir)/server/Makefile.inc

CFLAGS = $(pthread_includes) $(COMMON_CFLAGS) \
	-I$(GFUTIL_SRCDIR) -I$(GFSL_SRCDIR) -I$(GFARMLIB_SRCDIR) -I$(srcdir) \
	-I$(GFMD_SRCDIR) \
	$(metadb_client_includes) $(optional_cflags)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(metadb_client_libs) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

PROGRAM = db_journal_test

PRIVATE_RULE = $(PRIVATE_SERVER_GFMD_RULE)
PRIVATE_SRCS = $(PRIVATE_SERVER_GFMD_SRCS)
PRIVATE_FILES = $(PRIVATE_SERVER_GFMD_FILES)
PRIVATE_OBJS = $(PRIVATE_SERVER_GFMD_OBJS)
PUBLIC_RULE  = /dev/null
PUBLIC_SRCS  =
PUBLIC_OBJS  =

SRCS = \
	$(GFMD_SRCDIR)/abstract_host.c \
	$(GFMD_SRCDIR)/acl.c \
	$(GFMD_SRCDIR)/back_channel.c \
	$(GFMD_SRCDIR)/callout.c \
	$(GFMD_SRCDIR)/db_access.c \
	$(GFMD_SRCDIR)/db_journal.c \
	$(GFMD_SRCDIR)/db_journal_apply.c \
	$(GFMD_SRCDIR)/db_none.c \
	$(GFMD_SRCDIR)/dead_file_copy.c \
	$(GFMD_SRCDIR)/dir.c \
	$(GFMD_SRCDIR)/file_replication.c \
	$(GFMD_SRCDIR)/group.c \
	$(GFMD_SRCDIR)/host.c \
	$(GFMD_SRCDIR)/inode.c \
	$(GFMD_SRCDIR)/job.c \
	$(GFMD_SRCDIR)/journal_file.c \
	$(GFMD_SRCDIR)/mdhost.c \
	$(GFMD_SRCDIR)/mdcluster.c \
	$(GFMD_SRCDIR)/netsendq.c \
	$(GFMD_SRCDIR)/gfmd_channel.c \
	$(GFMD_SRCDIR)/peer_watcher.c \
	$(GFMD_SRCDIR)/peer.c \
	$(GFMD_SRCDIR)/local_peer.c \
	$(GFMD_SRCDIR)/remote_peer.c \
	$(GFMD_SRCDIR)/process.c \
	$(GFMD_SRCDIR)/quota.c \
	$(GFMD_SRCDIR)/replica_check.c \
	$(GFMD_SRCDIR)/subr.c \
	$(GFMD_SRCDIR)/thrpool.c \
	$(GFMD_SRCDIR)/user.c \
	$(GFMD_SRCDIR)/watcher.c \
	$(GFMD_SRCDIR)/xattr.c \
	$(GFMD_SRCDIR)/relay.c \
	$(GFMD_SRCDIR)/fsngroup.c \
	$(GFMD_SRCDIR)/thrstatewait.c \
	db_journal_test.c empty_ops.c

OBJS =	\
	$(GFMD_BUILDDIR)/abstract_host.o \
	$(GFMD_BUILDDIR)/acl.o \
	$(GFMD_BUILDDIR)/back_channel.o \
	$(GFMD_BUILDDIR)/callout.o \
	$(GFMD_BUILDDIR)/db_access.o \
	$(GFMD_BUILDDIR)/db_journal_apply.o \
	$(GFMD_BUILDDIR)/db_none.o \
	$(GFMD_BUILDDIR)/dead_file_copy.o \
	$(GFMD_BUILDDIR)/dir.o \
	$(GFMD_BUILDDIR)/file_replication.o \
	$(GFMD_BUILDDIR)/group.o \
	$(GFMD_BUILDDIR)/host.o \
	$(GFMD_BUILDDIR)/inode.o \
	$(GFMD_BUILDDIR)/job.o \
	$(GFMD_BUILDDIR)/journal_file.o \
	$(GFMD_BUILDDIR)/mdhost.o \
	$(GFMD_BUILDDIR)/mdcluster.o \
	$(GFMD_BUILDDIR)/netsendq.o \
	$(GFMD_BUILDDIR)/gfmd_channel.o \
	$(GFMD_BUILDDIR)/peer_watcher.o \
	$(GFMD_BUILDDIR)/peer.o \
	$(GFMD_BUILDDIR)/local_peer.o \
	$(GFMD_BUILDDIR)/remote_peer.o \
	$(GFMD_BUILDDIR)/process.o \
	$(GFMD_BUILDDIR)/quota.o \
	$(GFMD_BUILDDIR)/replica_check.o \
	$(GFMD_BUILDDIR)/subr.o \
	$(GFMD_BUILDDIR)/thrpool.o \
	$(GFMD_BUILDDIR)/user.o \
	$(GFMD_BUILDDIR)/watcher.o \
	$(GFMD_BUILDDIR)/xattr.o \
	$(GFMD_BUILDDIR)/relay.o \
	$(GFMD_BUILDDIR)/fsngroup.o \
	$(GFMD_BUILDDIR)/thrstatewait.o \
	db_journal_test.o empty_ops.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk
include $(top_srcdir)/makes/gflog.mk

###

$(OBJS): $(DEPGFARMINC)

$(OBJS): $(DEPGFARMINC) \
	$(GFUTIL_SRCDIR)/gfutil.h \
	$(GFUTIL_SRCDIR)/hash.h \
	$(GFUTIL_SRCDIR)/id_table.h \
	$(GFUTIL_SRCDIR)/tree.h \
	$(GFUTIL_SRCDIR)/thrsubr.h \
	$(GFARMLIB_SRCDIR)/patmatch.h \
	$(GFARMLIB_SRCDIR)/gfp_xdr.h \
	$(GFARMLIB_SRCDIR)/io_fd.h \
	$(GFARMLIB_SRCDIR)/sockopt.h \
	$(GFARMLIB_SRCDIR)/auth.h \
	$(GFARMLIB_SRCDIR)/config.h \
	$(GFARMLIB_SRCDIR)/gfm_proto.h \
	$(GFARMLIB_SRCDIR)/gfj_client.h \
	$(GFARMLIB_SRCDIR)/timespec.h \
	$(GFMD_SRCDIR)/thrpool.h \
	$(GFMD_SRCDIR)/subr.h \
	$(GFMD_SRCDIR)/rpcsubr.h \
	$(GFMD_SRCDIR)/callout.h \
	$(GFMD_SRCDIR)/watcher.h \
	$(GFMD_SRCDIR)/user.h \
	$(GFMD_SRCDIR)/group.h \
	$(GFMD_SRCDIR)/host.h \
	$(GFMD_SRCDIR)/abstract_host.h \
	$(GFMD_SRCDIR)/abstract_host_impl.h \
	$(GFMD_SRCDIR)/peer_watcher.h \
	$(GFMD_SRCDIR)/peer.h \
	$(GFMD_SRCDIR)/peer_impl.h \
	$(GFMD_SRCDIR)/local_peer.h \
	$(GFMD_SRCDIR)/remote_peer.h \
	$(GFMD_SRCDIR)/dead_file_copy.h \
	$(GFMD_SRCDIR)/process.h \
	$(GFMD_SRCDIR)/job.h \
	$(GFMD_SRCDIR)/dir.h \
	$(GFMD_SRCDIR)/inode.h \
	$(GFMD_SRCDIR)/fs.h \
	$(GFMD_SRCDIR)/back_channel.h \
	$(GFMD_SRCDIR)/protocol_state.h \
	$(GFMD_SRCDIR)/quota.h \
	$(GFMD_SRCDIR)/replica_check.h \
	$(GFMD_SRCDIR)/xattr.h \
	$(GFMD_SRCDIR)/journal_file.h \
	$(GFMD_SRCDIR)/db_journal.h \
	$(GFMD_SRCDIR)/db_journal_apply.h

include $(optional_rule)
