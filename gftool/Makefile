top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

GFVOMS_SYNC_TARGETS = gfvoms-sync

include $(top_srcdir)/makes/var.mk
PRIVATE_TARGETS = $(PRIVATE_GFTOOL_SUBDIRS)
PUBLIC_TARGETS =

# gfrep -> gfprep -> gfpconcat -> gfcp order should be kept,
# due to the following dependency:
#	gfprep: depends on gfrep
#	gfpconcat: depends on gfprep
#	gfcp: depends on gfprep, gfpconcat

SUBDIRS = \
	gfarmbb \
	gfchmod \
	gfchown \
	gfcksum \
	gfrep \
	gfprep \
	gfpconcat \
	gfcp \
	gfdf \
	gfdump \
	gfexport \
	gfdirpath \
	gffilepath \
	gffindxmlattr \
	gfgetfacl \
	gfgroup \
	gfhost \
	gfjournal \
	gfjournaladmin \
	gfjournaldump \
	gfkey \
	gfln \
	gfls \
	gflsof \
	gfmdhost \
	gfmkdir \
	gfmv \
	gfncopy \
	gfpath \
	gfptar \
	gfquota \
	gfreg \
	gfrepcheck \
	gfrm \
	gfrmdir \
	gfsched \
	gfsetfacl \
	gfspoolgen \
	gfspoolinum \
	gfspoolmd5 \
	gfspoolpath \
	gfstat \
	gfstatus \
	gfsudo \
	gftest \
	gfuser \
	gfusage \
	$(gfvoms_sync_targets) \
	gfwhere \
	gfwhoami \
	gfxattr \
	config-gfarm \
	gfhostgroup \
	$(optional_targets)

#	gfifo \
#	gfpwd \
#	gfsck \
#	gfsetdir \
#	gfq \

include $(top_srcdir)/makes/subdir.mk
