top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

datadir=$(default_datadir)/gfarm/systest/common_scripts

DATA =	$(srcdir)/check_gfmd_connected \
	$(srcdir)/check_gfmd_master \
	$(srcdir)/check_gfmd_slave \
	$(srcdir)/check_journal_seqnum_max \
	$(srcdir)/failback \
	$(srcdir)/wait_for_boot \
	$(srcdir)/check_file_location \
	$(srcdir)/check_gfsd_registered \
	$(srcdir)/check_replica_num \
	$(srcdir)/generate_testdata

include $(top_srcdir)/makes/data.mk
