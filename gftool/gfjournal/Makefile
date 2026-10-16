top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir =.

include $(top_srcdir)/makes/var.mk

CFLAGS = $(pthread_includes) $(COMMON_CFLAGS) \
	-I$(GFUTIL_SRCDIR) -I$(GFSL_SRCDIR) -I$(GFARMLIB_SRCDIR) -I$(srcdir) \
	-I$(GFMD_SRCDIR) \
	$(metadb_client_includes) $(optional_cflags)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(metadb_client_libs) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

PROGRAM = gfjournal

PRIVATE_RULE = $(PRIVATE_SERVER_GFMD_RULE)
PRIVATE_SRCS = $(PRIVATE_SERVER_GFMD_SRCS)
PRIVATE_FILES = $(PRIVATE_SERVER_GFMD_FILES)
PRIVATE_OBJS = $(PRIVATE_SERVER_GFMD_OBJS)
PUBLIC_RULE  = /dev/null
PUBLIC_SRCS  =
PUBLIC_OBJS  =

SRCS =	$(GFMD_SRCDIR)/db_access.c \
	$(GFMD_SRCDIR)/db_none.c \
	$(GFMD_SRCDIR)/db_common.c \
	$(GFMD_SRCDIR)/subr.c \
	$(GFMD_SRCDIR)/journal_file.c \
	$(GFMD_SRCDIR)/db_journal.c \
	gfjournal.c

OBJS =	$(GFMD_BUILDDIR)/db_access.o \
	$(GFMD_BUILDDIR)/db_common.o \
	$(GFMD_BUILDDIR)/db_none.o \
	$(GFMD_BUILDDIR)/subr.o \
	$(GFMD_BUILDDIR)/journal_file.o \
	$(GFMD_BUILDDIR)/db_journal.o \
	gfjournal.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) $(GFARMLIB_SRCDIR)/context.h $(GFARMLIB_SRCDIR)/gfm_proto.h $(GFARMLIB_SRCDIR)/metadb_common.h $(GFARMLIB_SRCDIR)/metadb_server.h $(GFARMLIB_SRCDIR)/quota_info.h $(GFARMLIB_SRCDIR)/xattr_info.h $(GFUTIL_SRCDIR)/assert.h $(GFUTIL_SRCDIR)/nanosec.h $(GFMD_SRCDIR)/db_common.h $(GFMD_SRCDIR)/db_ops.h $(GFMD_SRCDIR)/internal_host_info.h $(GFMD_SRCDIR)/quota.h

$(OBJS): $(DEPGFARMINC) \
	$(GFUTIL_SRCDIR)/gfutil.h \
	$(GFUTIL_SRCDIR)/hash.h \
	$(GFUTIL_SRCDIR)/id_table.h \
	$(GFUTIL_SRCDIR)/tree.h \
	$(GFUTIL_SRCDIR)/thrsubr.h \
	$(GFARMLIB_SRCDIR)/gfp_xdr.h \
	$(GFARMLIB_SRCDIR)/config.h \
	$(GFMD_SRCDIR)/subr.h \
	$(GFMD_SRCDIR)/db_access.h \
	$(GFMD_SRCDIR)/db_journal.h \
	$(GFMD_SRCDIR)/journal_file.h
