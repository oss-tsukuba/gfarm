#
# Makefile for the Linux Gfarm filesystem routines.
#
#obj-$(CONFIG_GFARM2_FS) := gfarm2.o
GFARMDIR =../..
LOCALDIR =..
obj-m := gfarm2.o
##s gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/auth_client.o
##s gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/auth_client_gsi.o
##s gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/auth_common.o
##s gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/auth_common_gsi.o
##s gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/auth_config.o
##s gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/auth_server.o
##s gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/auth_server_gsi.o
##s gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/auth_server_uid.o
##s gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/auth_server_uid_gsi.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/config.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/config_client.o
##s gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/config_server.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/conn_cache.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/conn_hash.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/context.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/crc32.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/filesystem.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfarm_foreach.o
##gfarm2fs  gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfarm_path.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfm_client.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfm_conn_follow.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfm_schedule.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfp_xdr.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfp_xdr_client.o
##s gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfp_xdr_server.o
##a gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_acl.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_attrplus.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_chmod.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_chown.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_client.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_dir.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_dircache.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_dirplus.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_dirplusxattr.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_io.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_link.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_mkdir.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_pio.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_pio_failover.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_pio_local.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_pio_remote.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_pio_section.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_profile.o
## gsi? gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_proto.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_quota.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_readlink.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_realpath.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_remove.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_rename.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_replica.o
##t gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_replica_info.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_replicate.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_rmdir.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_stat.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_statfs.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_statfsnode.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_statsw.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_symlink.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_unlink.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_util.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_utimes.o
##2 gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/gfs_xattr.o
##apl gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/glob.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/host.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/hostspec.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/humanize_number.o
##apl gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/import_help.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/iobuffer.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/io_fd.o
##2 gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/io_gfsl.o
## gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/iobuffer.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/liberror.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/lookup.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/metadb_common.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/metadb_server.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/param.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/patmatch.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/schedule.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/sockopt.o
##user gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/sockutil.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/stringlist.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/timespec.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfarm/url.o

gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/alloc.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/assert.o
## gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/daemon.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/gfevent.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/gfnetdb.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/hash.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/hash_strptr.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/id_table.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/limit.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/logutil.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/lru_cache.o
## gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/nanosec.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/random.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/send_no_sigpipe.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/sleep.o
##server gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/thrbarrier.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/thrsubr.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/timer.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/timeval.o
gfarm2-y += $(GFARMDIR)/lib/libgfarm/gfutil/utf8.o

gfarm2-y += ug_idmap.o
gfarm2-y += ug_proc.o

gfarm2-y += gfsk_dev.o
gfarm2-y += gfsk_devif.o
gfarm2-y += gfsk_netdb.o
gfarm2-y += gfsk_stdlib.o
gfarm2-y += gfsk_fd.o
gfarm2-y += gfsk_ioapi.o
gfarm2-y += gfsk_if.o
gfarm2-y += gfsk_gfarm.o
gfarm2-y += gfsk_eventfd.o
gfarm2-y += gfarm_stub.o

gfarm2-y += gfsk_super.o
gfarm2-y += gfsk_inode.o
gfarm2-y += gfsk_dir.o
gfarm2-y += gfsk_file.o
gfarm2-y += gfsk_genfile.o
gfarm2-y += gfsk_libgfarm.o

EXTRA_CFLAGS += -std=gnu89
EXTRA_CFLAGS += -I$(src)/$(GFARMDIR)/include
EXTRA_CFLAGS += -I$(src)/$(GFARMDIR)/lib/libgfarm/gfutil
EXTRA_CFLAGS += -I$(src)/$(GFARMDIR)/lib/libgfarm/gfarm
EXTRA_CFLAGS += -I$(src)
EXTRA_CFLAGS += -I$(src)/usr
EXTRA_CFLAGS += -I$(src)/$(LOCALDIR)/include

#REAL_ROOTDIR  := /usr/src/kernels/`uname -r`
REAL_ROOTDIR  := /lib/modules/`uname -r`/build
UM_ROOTDIR  := $(HOME)/workspace/linux-2.6.32
KBUILD_CFLAGS += -g -O0
PWD   := $(shell pwd)

all:
	$(MAKE) -C $(REAL_ROOTDIR) M=$(PWD)  KBUILD_VERBOSE=1 modules

install:
	$(MAKE) -C $(REAL_ROOTDIR) M=$(PWD) modules_install

umltest:
	$(MAKE) -C $(UM_ROOTDIR) M=$(PWD) KBUILD_VERBOSE=0 ARCH=um CONFIG_DEBUG_SECTION_MISMATCH=y CFLAGS="-g -O0" modules

clean:
	$(MAKE) -C $(REAL_ROOTDIR) M=$(PWD) KBUILD_VERBOSE=1 clean
	rm -f $(gfarm2-y)
	rm -f $(GFARMDIR)/lib/libgfarm/gfarm/.*.o.*
	rm -f $(GFARMDIR)/lib/libgfarm/gfutil/.*.o.*

distclean: clean
