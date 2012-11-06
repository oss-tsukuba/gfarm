top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

LOG=			log

CHECK_ENV=	LANG=C PATH=$(prefix)/bin:$$PATH

# subdirectories which have to be built
SUBDIRS=	\
	lib/libgfarm/gfutil/utf8 \
	lib/libgfarm/gfarm/empty_acl \
	lib/libgfarm/gfarm/gfarm_error_range_alloc \
	lib/libgfarm/gfarm/gfarm_error_to_errno \
	lib/libgfarm/gfarm/gfs_dir_test \
	lib/libgfarm/gfarm/gfs_pio_test \
	lib/libgfarm/gfarm/gfs_replicate_file_from_to_request/file_busy \
	lib/libgfarm/gfarm/gfs_replicate_file_from_to_request/in_progress \
	lib/libgfarm/gfarm/gfs_stat_cached \
	lib/libgfarm/gfarm/gfs_xattr \
	lib/libgfarm/gfarm/gfs_getxattr_cached \
	lib/libgfarm/gfarm/gfm_inode_or_name_op_test \
	server/gfmd/db_journal \
	manual/lib/libgfarm/gfarm/gfs_pio_failover

check test: all
	@echo ""
	@$(CHECK_ENV) $(srcdir)/regress.sh
	@echo ""

check-jenkins: all
	@echo ""
	@$(CHECK_ENV) $(srcdir)/regress.sh -j "${JENKINS_CLASSNAME}"
	@echo ""

loop: all
	@@$(CHECK_ENV) $(srcdir)/loop.sh

all: config

config:
	test -f Makefile || \
	( \
	echo "regress=$(srcdir)"; \
	echo ". $(srcdir)/regress.conf"; \
	) > regress.conf

distclean:
	-$(RM) -f $(LOG)
	-test -f Makefile || $(RM) -f regress.conf

include $(top_srcdir)/makes/subdir.mk
