# $Id$

top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

CFLAGS = $(COMMON_CFLAGS) $(openssl_includes) -I. $(gfsl_includes)

LIB = gfarm
GLOBUS_SRCS = auth_client_gsi.c auth_server_gsi.c io_gfsl.c
GLOBUS_OBJS = auth_client_gsi.o auth_server_gsi.o io_gfsl.o
SRCS =	auth_client.c auth_common.c auth_config.c auth_server.c \
	config.c error.c gfm_client.o gfm_proto.c gfs_client.c \
	gfs_pio.c gfs_pio_default.c gfs_pio_global.c \
	gfs_pio_local.c gfs_pio_remote.c gfs_pio_misc.c gfs_pio_section.c \
	gfs_proto.c hash.c host.c hostspec.c iobuffer.c \
	import_help.c logutil.c metadb_gfmd.c metadb_misc.c schedule.c \
	stringlist.c url.c xxx_proto.c io_fd.c \
	$(globus_srcs)
OBJS =	auth_client.o auth_common.o auth_config.o auth_server.o \
	config.o error.o gfm_client.o gfm_proto.o gfs_client.o \
	gfs_pio.o gfs_pio_default.o gfs_pio_global.o \
	gfs_pio_local.o gfs_pio_remote.o gfs_pio_misc.o gfs_pio_section.o \
	gfs_proto.o hash.o host.o hostspec.o iobuffer.o \
	import_help.o logutil.o metadb_gfmd.o metadb_misc.o schedule.o \
	stringlist.o url.o xxx_proto.o io_fd.o \
	$(globus_objs)

all: lib$(LIB).a

lib$(LIB).a: $(OBJS)
	ar rucv lib$(LIB).a $(OBJS)
	$(RANLIB) lib$(LIB).a

install: all
	$(INSTALL_DATA) lib$(LIB).a $(libdir)

clean:; rm -f *.o

veryclean: clean
	rm -f lib$(LIB).a

distclean: veryclean

###

$(OBJS): $(INCDIR)/gfarm.h $(INCDIR)/gfarm_error.h $(INCDIR)/gfarm_metadb.h $(INCDIR)/gfarm_misc.h $(INCDIR)/gfs.h hash.h iobuffer.h xxx_proto.h gfs_proto.h gfs_client.h
$(GLOBUS_OBJS): $(DEPGFSLINC)
