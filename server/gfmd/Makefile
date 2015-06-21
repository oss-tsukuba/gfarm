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

# the followings are used by regress/server/gfmd/db_journal/Makefile too:
include $(srcdir)/Makefile.inc

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

SRCS =	$(GFMD_COMMON_SRCS) \
	db_common.c \
	db_journal.c \
	fs.c \
	failover_notify.c \
	gfmd.c \
	$(ldap_srcs) $(postgresql_srcs) $(optional_srcs)
OBJS =	$(GFMD_COMMON_OBJS) \
	db_common.o \
	db_journal.o \
	fs.o \
	failover_notify.o \
	gfmd.o \
	$(ldap_objs) $(postgresql_objs) $(optional_objs)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk
include $(top_srcdir)/makes/gflog.mk

###

$(OBJS): $(DEPGFARMINC) $(DEPGFMDINC) \
	db_common.h \
	fs.h \
	job.h \
	replica_check.h \
	gfmd.h

include $(optional_rule)
