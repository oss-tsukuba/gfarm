# $Id$

top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk
PRIVATE_RULE = $(PRIVATE_LIB_LIBGFARM_GFARM_RULE)
PRIVATE_SRCS = $(PRIVATE_LIB_LIBGFARM_GFARM_SRCS)
PRIVATE_FILES = $(PRIVATE_LIB_LIBGFARM_GFARM_FILES)
PRIVATE_OBJS = $(PRIVATE_LIB_LIBGFARM_GFARM_OBJS)
PUBLIC_RULE  = /dev/null
PUBLIC_SRCS  =
PUBLIC_OBJS  =

LIBRARY = libgfarmcore.la
LIBRARY_RESULT = # do not install

GLOBUS_SRCS = auth_client_gsi.c auth_common_gsi.c \
	auth_server_gsi.c auth_server_uid_gsi.c io_gfsl.c
GLOBUS_OBJS = auth_client_gsi.lo auth_common_gsi.lo \
	auth_server_gsi.lo auth_server_uid_gsi.lo io_gfsl.lo
GLOBUS_CFLAGS = -I$(GFSL_SRCDIR) $(globus_includes)

SRCS = \
	context.c \
	crc32.c \
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
	gfp_xdr_server.c \
	gfs_proto.c \
	io_fd.c \
	metadb_common.c \
	metadb_server.c \
	auth_common.c \
	auth_client.c \
	auth_server.c \
	auth_server_uid.c \
	conn_hash.c \
	conn_cache.c \
	filesystem.c \
	gfm_client.c \
	gfs_client.c \
	gfm_conn_follow.c \
	gfm_schedule.c \
	lookup.c \
	gfs_io.c \
	gfs_dir.c \
	gfs_dirplus.c \
	gfs_dirplusxattr.c \
	gfs_dircache.c \
	gfs_attrplus.c \
	gfs_pio.c \
	gfs_pio_section.c \
	gfs_pio_local.c gfs_pio_remote.c \
	gfs_pio_failover.c \
	gfs_profile.c \
	gfs_chmod.c \
	gfs_chown.c \
	gfs_link.c \
	gfs_stat.c \
	gfs_statfs.c \
	gfs_statfsnode.c \
	gfs_statsw.c \
	gfs_mkdir.c \
	gfs_quota.c \
	gfs_readlink.c \
	gfs_realpath.c \
	gfs_remove.c \
	gfs_rename.c \
	gfs_replica.c \
	gfs_replica_info.c \
	gfs_replicate.c \
	gfs_rmdir.c \
	gfs_symlink.c \
	gfs_unlink.c \
	gfs_util.c \
	gfs_utimes.c \
	gfs_xattr.c \
	gfs_acl.c \
	gfarm_foreach.c \
	gfarm_path.c \
	glob.c \
	humanize_number.c \
	patmatch.c \
	schedule.c \
	sockutil.c \
	timespec.c \
	url.c \
	iostat.c \
	$(globus_srcs) \
	$(optional_srcs)

#	$(ldap_srcs) $(postgresql_srcs)

OBJS = \
	context.lo \
	crc32.lo \
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
	gfp_xdr_server.lo \
	gfs_proto.lo \
	io_fd.lo \
	metadb_common.lo \
	metadb_server.lo \
	auth_common.lo \
	auth_client.lo \
	auth_server.lo \
	auth_server_uid.lo \
	conn_hash.lo \
	conn_cache.lo \
	filesystem.lo \
	gfm_client.lo \
	gfs_client.lo \
	gfm_conn_follow.lo \
	gfm_schedule.lo \
	lookup.lo \
	gfs_io.lo \
	gfs_dir.lo \
	gfs_dirplus.lo \
	gfs_dirplusxattr.lo \
	gfs_dircache.lo \
	gfs_attrplus.lo \
	gfs_pio.lo \
	gfs_pio_section.lo \
	gfs_pio_local.lo gfs_pio_remote.lo \
	gfs_pio_failover.lo \
	gfs_profile.lo \
	gfs_chmod.lo \
	gfs_chown.lo \
	gfs_link.lo \
	gfs_stat.lo \
	gfs_statfs.lo \
	gfs_statfsnode.lo \
	gfs_statsw.lo \
	gfs_mkdir.lo \
	gfs_quota.lo \
	gfs_readlink.lo \
	gfs_realpath.lo \
	gfs_remove.lo \
	gfs_rename.lo \
	gfs_replica.lo \
	gfs_replica_info.lo \
	gfs_replicate.lo \
	gfs_rmdir.lo \
	gfs_symlink.lo \
	gfs_unlink.lo \
	gfs_util.lo \
	gfs_utimes.lo \
	gfs_xattr.lo \
	gfs_acl.lo \
	gfarm_foreach.lo \
	gfarm_path.lo \
	glob.lo \
	humanize_number.lo \
	patmatch.lo \
	schedule.lo \
	sockutil.lo \
	timespec.lo \
	url.lo \
	iostat.lo \
	$(globus_objs) \
	$(optional_objs)

#	$(ldap_objs) $(postgresql_objs)

CFLAGS = $(pthread_includes) \
	$(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) -I$(srcdir) \
	$(globus_cflags) $(metadb_client_includes) $(openssl_includes) \
	$(optional_cflags) \
	-DGFARM_CONFIG='"$(sysconfdir)/gfarm2.conf"'

all: $(LIBRARY)

include $(top_srcdir)/makes/lib.mk
include $(top_srcdir)/makes/gflog.mk

$(OBJS): $(DEPGFARMINC)
$(GLOBUS_OBJS): $(DEPGFSLINC)
auth_client.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/gfevent.h context.h liberror.h gfp_xdr.h auth.h
auth_client_gsi.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/gfevent.h $(GFSL_SRCDIR)/gfarm_secure_session.h $(GFSL_SRCDIR)/gfarm_auth.h liberror.h gfp_xdr.h io_fd.h io_gfsl.h auth.h auth_gsi.h gfs_proto.h
auth_common.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/thrsubr.h context.h liberror.h auth.h
auth_common_gsi.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/thrsubr.h $ $(GFSL_SRCDIR)/gfarm_secure_session.h $(GFSL_SRCDIR)/gfarm_auth.h context.h liberror.h gfpath.h auth.h auth_gsi.h
auth_config.lo: context.h liberror.h hostspec.h auth.h
auth_server.lo: $(GFUTIL_SRCDIR)/gfutil.h context.h liberror.h hostspec.h auth.h gfp_xdr.h gfs_proto.h gfm_proto.h
auth_server_gsi.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFSL_SRCDIR)/gfarm_secure_session.h $(GFSL_SRCDIR)/gfarm_auth.h liberror.h gfp_xdr.h io_fd.h io_gfsl.h auth.h auth_gsi.h gfs_proto.h
auth_server_uid.lo: $(GFUTIL_SRCDIR)/gfutil.h auth.h gfm_client.h
auth_server_uid_gsi.lo: $(GFUTIL_SRCDIR)/gfutil.h auth.h gfm_client.h
config.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/hash.h $(GFUTIL_SRCDIR)/lru_cache.h context.h liberror.h patmatch.h hostspec.h param.h sockopt.h host.h auth.h gfpath.h config.h gfm_proto.h gfs_proto.h gfs_profile.h gfm_client.h lookup.h metadb_server.h filesystem.h conn_hash.h conn_cache.h $(top_builddir)/makes/config.mk # $(GFARM_CONFIG) -> $(sysconfdir)
config_client.lo: $(GFUTIL_SRCDIR)/timer.h $(GFUTIL_SRCDIR)/gfutil.h context.h liberror.h gfs_profile.h host.h auth.h gfpath.h config.h gfm_client.h gfs_proto.h gfs_client.h lookup.h filesystem.h metadb_server.h
config_server.lo: $(GFUTIL_SRCDIR)/gfutil.h context.h liberror.h auth.h gfpath.h config.h
conn_cache.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/hash.h $(GFUTIL_SRCDIR)/lru_cache.h $(GFUTIL_SRCDIR)/thrsubr.h conn_hash.h conn_cache.h
conn_hash.lo: $(GFUTIL_SRCDIR)/hash.h conn_hash.h
context.lo: context.h config.h
error.lo: $(GFUTIL_SRCDIR)/hash.h
crc32.lo: crc32.h
filesystem.lo: $(GFUTIL_SRCDIR)/hash.h context.h filesystem.h metadb_server.h gfm_client.h gfs_file_list.h
gfm_client.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/hash.h $(GFUTIL_SRCDIR)/gfnetdb.h $(GFUTIL_SRCDIR)/lru_cache.h $(GFUTIL_SRCDIR)/queue.h context.h gfp_xdr.h io_fd.h sockopt.h sockutil.h host.h auth.h config.h conn_cache.h gfm_proto.h gfj_client.h xattr_info.h gfm_client.h quota_info.h metadb_server.h filesystem.h liberror.h
gfm_conn_follow.lo: gfm_client.h lookup.h
gfm_schedule.lo: gfm_client.h gfm_schedule.h gfs_failover.h lookup.h
gfp_xdr.lo: $(GFUTIL_SRCDIR)/gfutil.h liberror.h iobuffer.h gfp_xdr.h
gfp_xdr_server.lo: $(GFUTIL_SRCDIR)/id_table.h $(GFUTIL_SRCDIR)/thrsubr.h liberror.h gfp_xdr.h
gfs_acl.lo:
gfs_chmod.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/timer.h context.h gfs_profile.h gfm_client.h lookup.h
gfs_chown.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/timer.h context.h gfs_profile.h gfm_client.h lookup.h
gfs_client.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/gfevent.h $(GFUTIL_SRCDIR)/hash.h $(GFUTIL_SRCDIR)/lru_cache.h context.h liberror.h sockutil.h iobuffer.h gfp_xdr.h io_fd.h host.h sockopt.h auth.h config.h conn_cache.h gfs_proto.h gfs_client.h gfm_client.h iostat.h filesystem.h gfs_failover.h
gfs_dir.lo: $(GFUTIL_SRCDIR)/timer.h $(GFUTIL_SRCDIR)/gfutil.h gfs_profile.h gfm_client.h config.h lookup.h gfs_io.h gfs_dir.h
gfs_dirplus.lo: $(GFUTIL_SRCDIR)/gfutil.h config.h gfm_client.h lookup.h gfs_io.h
gfs_dirplusxattr.lo: $(GFUTIL_SRCDIR)/gfutil.h config.h gfm_client.h gfs_io.h gfs_dirplusxattr.h
gfs_dircache.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/hash.h context.h config.h gfs_dir.h gfs_dirplusxattr.h gfs_dircache.h gfs_attrplus.h
gfs_attrplus.lo: $(GFUTIL_SRCDIR)/gfutil.h gfm_client.h config.h lookup.h gfs_attrplus.h
gfs_io.lo: $(GFUTIL_SRCDIR)/gfutil.h gfm_client.h lookup.h gfs_io.h
gfs_link.lo: context.h gfm_client.h lookup.h
gfs_mkdir.lo: $(GFUTIL_SRCDIR)/gfutil.h gfm_client.h config.h lookup.h
gfs_pio.lo: $(GFUTIL_SRCDIR)/timer.h $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/queue.h $(GFUTIL_SRCDIR)/thrsubr.h context.h liberror.h filesystem.h gfs_profile.h gfm_client.h gfs_proto.h gfs_io.h gfs_pio.h gfp_xdr.h gfs_failover.h gfs_file_list.h
gfs_pio_local.lo: $(GFUTIL_SRCDIR)/queue.h gfs_proto.h gfs_client.h gfs_io.h gfs_pio.h
gfs_pio_remote.lo: $(GFUTIL_SRCDIR)/queue.h host.h config.h gfs_proto.h gfs_client.h gfs_io.h gfs_pio.h
gfs_pio_section.lo: $(GFUTIL_SRCDIR)/timer.h $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/queue.h context.h liberror.h gfs_profile.h host.h config.h gfm_client.h gfm_schedule.h gfs_client.h gfs_proto.h gfs_io.h gfs_pio.h schedule.h filesystem.h gfs_failover.h
gfs_pio_failover.lo: $(GFUTIL_SRCDIR)/queue.h config.h gfm_client.h gfs_client.h gfs_io.h gfs_pio.h filesystem.h gfs_failover.h gfs_file_list.h
gfs_profile.lo: $(GFUTIL_SRCDIR)/timer.h context.h
gfs_proto.lo: gfs_proto.h
gfs_quota.lo: config.h quota_info.h
gfs_readlink.lo: $(GFUTIL_SRCDIR)/gfutil.h gfm_client.h config.h lookup.h
gfs_realpath.lo: gfm_client.h lookup.h
gfs_remove.lo: $(GFUTIL_SRCDIR)/gfutil.h context.h gfm_client.h lookup.h
gfs_rename.lo: context.h gfm_client.h lookup.h
gfs_replica.lo: $(GFUTIL_SRCDIR)/gfutil.h gfm_client.h config.h lookup.h
gfs_replica_info.lo: gfm_proto.h gfm_client.h lookup.h
gfs_replicate.lo: config.h host.h gfm_client.h gfs_client.h lookup.h schedule.h gfs_misc.h gfs_failover.h
gfs_rmdir.lo:
gfs_stat.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/timer.h context.h gfs_profile.h gfm_client.h lookup.h gfs_failover.h
gfs_statfs.lo: gfm_client.h lookup.h config.h
gfs_statfsnode.lo: config.h gfm_client.h gfs_client.h lookup.h
gfs_statsw.lo: gfs_dircache.h
gfs_symlink.lo: $(GFUTIL_SRCDIR)/gfutil.h context.h gfm_client.h lookup.h
gfs_unlink.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/timer.h context.h gfs_profile.h
gfs_util.lo:
gfs_utimes.lo: $(GFUTIL_SRCDIR)/gfutil.h config.h gfm_client.h lookup.h
gfs_xattr.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/timer.h context.h gfm_client.h lookup.h gfs_io.h gfs_misc.h gfs_profile.h xattr_info.h
gfs_acl.lo: $(GFUTIL_SRCDIR)/gfutil.h
gfarm_foreach.lo: gfarm_foreach.h
gfarm_path.lo: gfarm_path.h
glob.lo: $(GFUTIL_SRCDIR)/gfutil.h liberror.h patmatch.h
humanize_number.lo:
host.lo: $(GFUTIL_SRCDIR)/gfnetdb.h context.h hostspec.h gfm_client.h host.h
hostspec.lo: $(GFUTIL_SRCDIR)/gfnetdb.h liberror.h hostspec.h
import_help.lo: liberror.h
io_fd.lo: context.h iobuffer.h gfp_xdr.h io_fd.h config.h
io_gfsl.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFSL_SRCDIR)/gfarm_secure_session.h context.h liberror.h iobuffer.h gfp_xdr.h io_fd.h io_gfsl.h config.h
iobuffer.lo: iobuffer.h crc32.h
liberror.lo: $(GFUTIL_SRCDIR)/gfutil.h liberror.h gfpath.h
lookup.lo: $(GFUTIL_SRCDIR)/gfutil.h context.h config.h gfm_client.h lookup.h gfs_failover.h
metadb_common.lo: metadb_common.h xattr_info.h quota_info.h metadb_server.h
metadb_server.lo: gfm_proto.h metadb_server.h
patmatch.lo: patmatch.h
param.lo: liberror.h hostspec.h param.h
schedule.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/gfevent.h $(GFUTIL_SRCDIR)/hash.h $(GFUTIL_SRCDIR)/timer.h context.h liberror.h conn_hash.h host.h config.h gfm_proto.h gfm_client.h gfs_client.h schedule.h lookup.h gfs_profile.h
sockutil.lo: $(GFUTIL_SRCDIR)/gfnetdb.h sockutil.h
sockopt.lo: context.h liberror.h hostspec.h param.h sockopt.h
stringlist.lo:
timespec.lo: timespec.h
url.lo: $(GFUTIL_SRCDIR)/queue.h config.h gfs_pio.h

include $(optional_rule)
