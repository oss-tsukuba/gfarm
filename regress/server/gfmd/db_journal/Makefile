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

SRCS = $(GFMD_COMMON_SRCS) \
	db_journal_test.c empty_ops.c

OBJS = $(GFMD_COMMON_OBJS) \
	db_journal_test.o empty_ops.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk
include $(top_srcdir)/makes/gflog.mk
include $(GFMD_SRCDIR)/Makefile.inc

###

$(OBJS): $(DEPGFARMINC) $(GFARMLIB_SRCDIR)/crc32.h $(GFARMLIB_SRCDIR)/metadb_server.h $(GFARMLIB_SRCDIR)/quota_info.h $(GFARMLIB_SRCDIR)/xattr_info.h $(GFUTIL_SRCDIR)/assert.h $(GFUTIL_SRCDIR)/thrbarrier.h db_journal_test.h $(GFMD_SRCDIR)/db_common.h

$(OBJS): $(DEPGFARMINC) $(DEPGFMDINC)

include $(optional_rule)
