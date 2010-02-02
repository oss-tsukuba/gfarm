# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk
include $(srcdir)/../Makefile.inc

CFLAGS = $(pthread_includes) \
	$(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) -I$(srcdir) \
	$(metadb_client_includes) $(optional_cflags) \
	-DGFMD_CONFIG='"$(sysconfdir)/gfmd.conf"'
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(metadb_client_libs) \
	$(LIBS) $(pthread_libs)
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

SRCS =	gfmd.c thrsubr.c thrpool.c \
	subr.c id_table.c user.c group.c host.c peer.c \
	process.c job.c \
	dir.c inode.c fs.c back_channel.c \
	db_access.c db_common.c db_none.c quota.c xattr.c \
	$(ldap_srcs) $(postgresql_srcs) $(optional_srcs)
OBJS =	gfmd.o thrsubr.o thrpool.o \
	subr.o id_table.o user.o group.o host.o peer.o \
	process.o job.o \
	dir.o inode.o fs.o back_channel.o \
	db_access.o db_common.o db_none.o quota.o xattr.o \
	$(ldap_objs) $(postgresql_objs) $(optional_objs)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk
include $(top_srcdir)/makes/gflog.mk

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
	$(GFARMLIB_SRCDIR)/timespec.h \
	thrsubr.h thrpool.h subr.h id_table.h \
	user.h group.h host.h peer.h process.h job.h \
	dir.h inode.h fs.h back_channel.h protocol_state.h quota.h xattr.h

include $(optional_rule)
