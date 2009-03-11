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
	liberror.c \
	hostspec.c \
	host.c \
	param.c \
	sockopt.c \
	auth_config.c \
	stringlist.c \
	config.c \
	config_server.c \
	config_client.c \
	import_help.c \
	iobuffer.c \
	gfp_xdr.c \
	gfs_proto.c \
	io_fd.c \
	metadb_common.c \
	metadb_server.c \
	auth_common.c \
	auth_client.c \
	auth_server.c \
	conn_hash.c \
	gfm_client.c \
	gfs_client.c \
	lookup.c \
	gfs_io.c \
	gfs_dir.c \
	gfs_dirplus.c \
	gfs_dircache.c \
	gfs_pio.c \
	gfs_pio_section.c \
	gfs_pio_local.c gfs_pio_remote.c \
	gfs_profile.c \
	gfs_chmod.c \
	gfs_chown.c \
	gfs_link.c \
	gfs_stat.c \
	gfs_statfs.c \
	gfs_statsw.c \
	gfs_mkdir.c \
	gfs_rmdir.c \
	gfs_remove.c \
	gfs_rename.c \
	gfs_replica.c \
	gfs_replicate.c \
	gfs_unlink.c \
	gfs_utimes.c \
	gfarm_foreach.c \
	glob.c \
	schedule.c \
	timespec.c \
	url.c \
	$(globus_srcs)

#	gfs_pio_global.c \
#	gfs_pio_misc.c \
#	gfs_exec.c \
#	import_help.c \

#	$(ldap_srcs) $(postgresql_srcs) $(globus_srcs)

OBJS = \
	liberror.lo \
	hostspec.lo \
	host.lo \
	param.lo \
	sockopt.lo \
	auth_config.lo \
	stringlist.lo \
	config.lo \
	config_server.lo \
	config_client.lo \
	import_help.lo \
	iobuffer.lo \
	gfp_xdr.lo \
	gfs_proto.lo \
	io_fd.lo \
	metadb_common.lo \
	metadb_server.lo \
	auth_common.lo \
	auth_client.lo \
	auth_server.lo \
	conn_hash.lo \
	gfm_client.lo \
	gfs_client.lo \
	lookup.lo \
	gfs_io.lo \
	gfs_dir.lo \
	gfs_dirplus.lo \
	gfs_dircache.lo \
	gfs_pio.lo \
	gfs_pio_section.lo \
	gfs_pio_local.lo gfs_pio_remote.lo \
	gfs_profile.lo \
	gfs_chmod.lo \
	gfs_chown.lo \
	gfs_link.lo \
	gfs_stat.lo \
	gfs_statfs.lo \
	gfs_statsw.lo \
	gfs_mkdir.lo \
	gfs_rmdir.lo \
	gfs_remove.lo \
	gfs_rename.lo \
	gfs_replica.lo \
	gfs_replicate.lo \
	gfs_unlink.lo \
	gfs_utimes.lo \
	gfarm_foreach.lo \
	glob.lo \
	schedule.lo \
	timespec.lo \
	url.lo \
	$(globus_objs)

#	gfs_pio_global.lo \
#	gfs_pio_misc.lo \
#	gfs_exec.lo \
#	import_help.lo \

#	$(ldap_srcs) $(postgresql_srcs) $(globus_srcs)

CFLAGS = $(pthread_includes) \
	$(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	$(globus_cflags) $(metadb_client_includes) $(openssl_includes) \
	-DGFARM_CONFIG='"$(sysconfdir)/gfarm2.conf"'

all: $(LIBRARY)

include $(top_srcdir)/makes/lib.mk

###

$(OBJS): $(DEPGFARMINC)
$(GLOBUS_OBJS): $(DEPGFSLINC)
auth_client.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/gfevent.h auth.h
auth_client_gsi.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/gfevent.h $(GFSL_SRCDIR)/gfarm_secure_session.h $(GFSL_SRCDIR)/gfarm_auth.h io_fd.h io_gfsl.h auth.h auth_gsi.h
auth_common.lo: auth.h
auth_common_gsi.lo: $(GFUTIL_SRCDIR)/gfutil.h $ $(GFSL_SRCDIR)/gfarm_secure_session.h $(GFSL_SRCDIR)/gfarm_auth.h gfpath.h auth.h auth_gsi.h
auth_config.lo: hostspec.h auth.h
auth_server.lo: $(GFUTIL_SRCDIR)/gfutil.h hostspec.h auth.h
auth_server_gsi.lo: $(GFSL_SRCDIR)/gfarm_secure_session.h $(GFSL_SRCDIR)/gfarm_auth.h $(GFUTIL_SRCDIR)/gfutil.h io_fd.h io_gfsl.h auth.h auth_gsi.h
config.lo: $(GFUTIL_SRCDIR)/gfutil.h gfpath.h hostspec.h host.h param.h sockopt.h auth.h config.h gfm_proto.h gfs_proto.h gfs_client.h gfs_pio.h $(GFUTIL_SRCDIR)/timer.h $(top_builddir)/makes/config.mk # $(GFARM_CONFIG) -> $(sysconfdir)
error.lo: $(GFUTIL_SRCDIR)/hash.h
gfm_client.lo: $(GFUTIL_SRCDIR)/hash.h io_fd.h sockopt.h auth.h config.h gfm_proto.h gfj_client.h
gfs_client.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/gfevent.h $(GFUTIL_SRCDIR)/hash.h iobuffer.h io_fd.h host.h param.h sockopt.h auth.h config.h gfs_proto.h gfs_client.h
gfs_dir.lo: $(GFUTIL_SRCDIR)/hash.h $(GFUTIL_SRCDIR)/gfutil.h gfs_pio.h
gfs_dirplus.lo: $(GFUTIL_SRCDIR)/gfutil.h config.h gfm_client.h gfs_io.h
gfs_dircache.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/hash.h config.h gfs_dircache.h
gfs_exec.lo: gfs_pio.h
gfs_io.lo: gfs_io.h
gfs_link.lo: $(GFUTIL_SRCDIR)/gfutil.h gfm_client.h config.h lookup.h
gfs_pio.lo: gfs_proto.h gfs_pio.h $(GFUTIL_SRCDIR)/timer.h
gfs_pio_global.lo: gfs_proto.h gfs_pio.h
gfs_pio_local.lo: gfs_proto.h gfs_pio.h
gfs_pio_misc.lo: host.h config.h gfs_proto.h gfs_client.h gfs_pio.h gfs_misc.h $(GFUTIL_SRCDIR)/timer.h
gfs_pio_remote.lo: host.h config.h gfs_proto.h gfs_client.h gfs_pio.h
gfs_pio_section.lo: gfs_pio.h host.h config.h schedule.h gfs_client.h gfs_proto.h $(GFUTIL_SRCDIR)/timer.h
gfs_proto.lo: gfs_proto.h
gfs_replica.lo: $(GFUTIL_SRCDIR)/gfutil.h gfm_client.h config.h lookup.h
gfs_replicate.lo: $(GFUTIL_SRCDIR)/gfutil.h config.h gfs_client.h
gfs_stat.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/timer.h gfs_profile.h gfm_client.h config.h lookup.h
gfs_statsw.lo: gfs_dircache.h
gfs_unlink.lo: config.h gfs_client.h gfs_pio.h gfs_misc.h $(GFUTIL_SRCDIR)/timer.h
gfs_utimes.lo: $(GFUTIL_SRCDIR)/gfutil.h config.h gfm_client.h lookup.h 
gfarm_foreach.lo: gfarm_foreach.h
glob.lo:
host.lo: hostspec.h host.h
hostspec.lo: hostspec.h
import_help.lo: liberror.h
io_fd.lo: iobuffer.h io_fd.h
io_gfsl.lo: $(GFSL_SRCDIR)/gfarm_secure_session.h iobuffer.h io_fd.h io_gfsl.h
iobuffer.lo: iobuffer.h
metadb_common.lo: metadb_common.h
param.lo: hostspec.h param.h
schedule.lo: $(GFUTIL_SRCDIR)/gfevent.h $(GFUTIL_SRCDIR)/hash.h host.h auth.h config.h gfs_client.h gfs_profile.h
sockopt.lo: hostspec.h param.h sockopt.h
stringlist.lo:
timespec.lo: timespec.h
url.lo: config.h gfs_pio.h
