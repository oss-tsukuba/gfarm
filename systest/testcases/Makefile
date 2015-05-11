top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

datadir=$(default_datadir)/gfarm/systest/testcases/gfarm_v2

DATA =	$(srcdir)/configuration \
	$(srcdir)/failover_by_stop \
	$(srcdir)/failover_by_kill \
	$(srcdir)/failover_by_reboot \
	$(srcdir)/failover_in_read \
	$(srcdir)/failover_in_write \
	$(srcdir)/failover_in_mkdir

include $(top_srcdir)/makes/data.mk
