# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk
include $(srcdir)/../Makefile.inc

CFLAGS = $(pthread_includes) $(COMMON_CFLAGS) \
	-I$(GFUTIL_SRCDIR) -I$(GFSL_SRCDIR) -I$(GFARMLIB_SRCDIR) -I$(srcdir) \
	$(metadb_client_includes) $(optional_cflags) \
	-DGFMD_CONFIG='"$(sysconfdir)/gfmd.conf"'
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(metadb_client_libs) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

PROGRAM = gfmd

LDAP_SRCS = db_ldap.c
LDAP_OBJS = db_ldap.lo
POSTGRESQL_SRCS = db_pgsql.c
POSTGRESQL_OBJS = db_pgsql.lo

PRIVATE_RULE = $(PRIVATE_SERVER_GFMD_RULE)
PRIVATE_SRCS = $(PRIVATE_SERVER_GFMD_SRCS)
PRIVATE_FILES = $(PRIVATE_SERVER_GFMD_FILES)
PRIVATE_OBJS = $(PRIVATE_SERVER_GFMD_OBJS)
PUBLIC_RULE  = /dev/null
PUBLIC_SRCS  =
PUBLIC_OBJS  =

SRCS =	gfmd.c thrpool.c callout.c subr.c rpcsubr.c watcher.c \
	user.c group.c host.c peer.c dead_file_copy.c \
	process.c job.c abstract_host.c replica_check.c \
	dir.c inode.c fs.c back_channel.c acl.c journal_file.c \
	mdhost.c gfmd_channel.c mdcluster.c \
	db_access.c db_common.c db_none.c quota.c xattr.c \
	db_journal.c db_journal_apply.c internal_host_info.c \
	fsngroup.c \
	$(ldap_srcs) $(postgresql_srcs) $(optional_srcs)
OBJS =	gfmd.o thrpool.o callout.o subr.o rpcsubr.o watcher.o \
	user.o group.o host.o peer.o dead_file_copy.o \
	process.o job.o abstract_host.o replica_check.o \
	dir.o inode.o fs.o back_channel.o acl.o journal_file.o \
	mdhost.o gfmd_channel.o mdcluster.o \
	db_access.o db_common.o db_none.o quota.o xattr.o \
	db_journal.o db_journal_apply.o internal_host_info.o \
	fsngroup.o \
	$(ldap_objs) $(postgresql_objs) $(optional_objs)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk
include $(top_srcdir)/makes/gflog.mk

###

$(OBJS): $(DEPGFARMINC) \
	$(GFUTIL_SRCDIR)/gfutil.h \
	$(GFUTIL_SRCDIR)/gflog_reduced.h \
	$(GFUTIL_SRCDIR)/hash.h \
	$(GFUTIL_SRCDIR)/id_table.h \
	$(GFUTIL_SRCDIR)/tree.h \
	$(GFUTIL_SRCDIR)/thrsubr.h \
	$(GFARMLIB_SRCDIR)/context.h \
	$(GFARMLIB_SRCDIR)/patmatch.h \
	$(GFARMLIB_SRCDIR)/gfp_xdr.h \
	$(GFARMLIB_SRCDIR)/io_fd.h \
	$(GFARMLIB_SRCDIR)/sockopt.h \
	$(GFARMLIB_SRCDIR)/auth.h \
	$(GFARMLIB_SRCDIR)/config.h \
	$(GFARMLIB_SRCDIR)/gfm_proto.h \
	$(GFARMLIB_SRCDIR)/gfj_client.h \
	$(GFARMLIB_SRCDIR)/timespec.h \
	thrpool.h subr.h rpcsubr.h callout.h watcher.h acl.h db_access.h \
	user.h group.h host.h peer.h dead_file_copy.h process.h job.h \
	dir.h inode.h fs.h back_channel.h protocol_state.h quota.h xattr.h \
	journal_file.h db_journal.h db_journal_apply.h abstract_host.h \
	gfmd_channel.h mdhost.h mdcluster.h replica_check.h fsngroup.h

include $(optional_rule)
