# $Id$

top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

LIBRARY = libgfarmcore.la
LIBRARY_RESULT = # do not install

GLOBUS_SRCS = auth_client_gsi.c auth_common_gsi.c auth_server_gsi.c io_gfsl.c
GLOBUS_OBJS = auth_client_gsi.lo auth_common_gsi.lo auth_server_gsi.lo \
	io_gfsl.lo
GLOBUS_CFLAGS = -I$(GFSL_SRCDIR) $(globus_includes)

SRCS = \
	auth_client.c auth_common.c auth_config.c auth_server.c \
	config.c error.c gfm_client.c gfs_client.c gfs_client_rep.c \
	gfs_dir.c gfs_pio.c gfs_pio_global.c \
	gfs_pio_local.c gfs_pio_remote.c gfs_pio_misc.c gfs_pio_section.c \
	gfs_exec.c gfs_unlink.c gfs_lock.c \
	gfs_proto.c glob.c host.c hostspec.c iobuffer.c \
	import_help.c metadb_ldap.c metadb_misc.c param.c schedule.c \
	sockopt.c stringlist.c url.c xxx_proto.c io_fd.c \
	$(globus_srcs)
OBJS = \
	auth_client.lo auth_common.lo auth_config.lo auth_server.lo \
	config.lo error.lo gfm_client.lo gfs_client.lo gfs_client_rep.lo \
	gfs_dir.lo gfs_pio.lo gfs_pio_global.lo \
	gfs_pio_local.lo gfs_pio_remote.lo gfs_pio_misc.lo gfs_pio_section.lo \
	gfs_exec.lo gfs_unlink.lo gfs_lock.lo \
	gfs_proto.lo glob.lo host.lo hostspec.lo iobuffer.lo \
	import_help.lo metadb_ldap.lo metadb_misc.lo param.lo schedule.lo \
	sockopt.lo stringlist.lo url.lo xxx_proto.lo io_fd.lo \
	$(globus_objs)

CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	$(globus_cflags) $(metadb_client_includes) $(openssl_includes)

all: $(LIBRARY)

include $(top_srcdir)/makes/lib.mk

###

$(GFARM_OBJS): $(DEPGFARMINC)
$(GFARM_GLOBUS_OBJS): $(DEPGFSLINC)
auth_client.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/gfevent.h xxx_proto.h auth.h
auth_client_gsi.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/gfevent.h $(GFSL_SRCDIR)/gfarm_secure_session.h $(GFSL_SRCDIR)/gfarm_auth.h xxx_proto.h io_fd.h io_gfsl.h auth.h auth_gsi.h
auth_common.lo: auth.h
auth_common_gsi.lo: $(GFUTIL_SRCDIR)/gfutil.h $ $(GFSL_SRCDIR)/gfarm_secure_session.h $(GFSL_SRCDIR)/gfarm_auth.h auth.h auth_gsi.h
auth_config.lo: hostspec.h auth.h
auth_server.lo: $(GFUTIL_SRCDIR)/gfutil.h xxx_proto.h hostspec.h auth.h
auth_server_gsi.lo: $(GFSL_SRCDIR)/gfarm_secure_session.h $(GFSL_SRCDIR)/gfarm_auth.h $(GFUTIL_SRCDIR)/gfutil.h xxx_proto.h io_fd.h io_gfsl.h auth.h auth_gsi.h
config.lo: $(GFUTIL_SRCDIR)/gfutil.h hostspec.h host.h param.h sockopt.h auth.h config.h gfm_proto.h gfs_proto.h gfs_client.h gfs_pio.h $(GFUTIL_SRCDIR)/timer.h
error.lo: $(GFUTIL_SRCDIR)/hash.h
gfm_client.lo: $(GFUTIL_SRCDIR)/hash.h xxx_proto.h io_fd.h sockopt.h auth.h config.h gfm_proto.h gfj_client.h
gfs_client.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/gfevent.h $(GFUTIL_SRCDIR)/hash.h iobuffer.h xxx_proto.h io_fd.h host.h param.h sockopt.h auth.h config.h gfs_proto.h gfs_client.h
gfs_client_rep.lo: $(GFUTIL_SRCDIR)/gfutil.h xxx_proto.h io_fd.h host.h param.h auth.h config.h gfs_proto.h gfs_client.h
gfs_dir.lo: $(GFUTIL_SRCDIR)/hash.h gfs_pio.h dircache.h
gfs_exec.lo: gfs_pio.h gfs_lock.h
gfs_lock.lo: 
gfs_pio.lo: gfs_proto.h gfs_pio.h $(GFUTIL_SRCDIR)/timer.h
gfs_pio_global.lo: gfs_proto.h gfs_pio.h
gfs_pio_local.lo: gfs_proto.h gfs_pio.h
gfs_pio_misc.lo: host.h config.h gfs_proto.h gfs_client.h gfs_pio.h gfs_misc.h $(GFUTIL_SRCDIR)/timer.h
gfs_pio_remote.lo: host.h config.h gfs_proto.h gfs_client.h gfs_pio.h
gfs_pio_section.lo: gfs_pio.h host.h config.h schedule.h gfs_client.h gfs_proto.h $(GFUTIL_SRCDIR)/timer.h gfs_lock.h
gfs_proto.lo: gfs_proto.h
gfs_unlink.lo: config.h gfs_client.h gfs_pio.h gfs_misc.h $(GFUTIL_SRCDIR)/timer.h
glob.lo:
host.lo: hostspec.h host.h
hostspec.lo: hostspec.h
import_help.lo:
io_fd.lo: iobuffer.h xxx_proto.h io_fd.h
io_gfsl.lo: $(GFSL_SRCDIR)/gfarm_secure_session.h iobuffer.h xxx_proto.h io_fd.h io_gfsl.h
iobuffer.lo: iobuffer.h
metadb_ldap.lo: config.h dircache.h
metadb_misc.lo: host.h config.h gfs_client.h
param.lo: hostspec.h param.h
schedule.lo: $(GFUTIL_SRCDIR)/gfevent.h $(GFUTIL_SRCDIR)/hash.h host.h auth.h config.h gfs_client.h
sockopt.lo: hostspec.h param.h sockopt.h
stringlist.lo:
url.lo: config.h gfs_pio.h
xxx_proto.lo: iobuffer.h xxx_proto.h
