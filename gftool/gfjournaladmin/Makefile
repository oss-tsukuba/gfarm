top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

datadir=$(default_datadir)/gfarm/ruby

BUILT_SCRIPTS = gfjournaladmin

DATA =	$(srcdir)/gfjournalfile.rb \
	$(srcdir)/gfcrc32.rb

include $(top_srcdir)/makes/script.mk
include $(top_srcdir)/makes/data.mk

gfjournaladmin: $(srcdir)/gfjournaladmin.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@
