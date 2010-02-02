top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

GFVOMS_SYNC_TARGETS = gfvoms-sync

include $(top_srcdir)/makes/var.mk
PRIVATE_TARGETS = $(PRIVATE_GFTOOL_SUBDIRS)
PUBLIC_TARGETS =

SUBDIRS = \
	gfchmod \
	gfchown \
	gfdf \
	gfdump \
	gfexport \
	gffindxmlattr \
	gfgroup \
	gfhost \
	gfkey \
	gfln \
	gfls \
	gfmkdir \
	gfmv \
	gfquota \
	gfreg \
	gfrep \
	gfrm \
	gfrmdir \
	gfstat \
	gfstatus \
	gfuser \
	gfusage \
	$(gfvoms_sync_targets) \
	gfwhere \
	gfwhoami \
	gfxattr \
	config-gfarm \
	$(optional_targets)

#	gfexec \
#	gfifo \
#	gfpwd \
#	gfrcmd \
#	gfsched \
#	gfsck \
#	gfsetdir \
#	gfmpirun \
#	gfps \
#	gfq \
#	gfrun \
#	gftest \
#	example \

include $(top_srcdir)/makes/subdir.mk
