top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SCRIPTS = gfnop-journal
BUILT_SCRIPTS = gfnop-journal

include $(top_srcdir)/makes/script.mk

gfnop-journal: $(srcdir)/gfnop-journal.in
	$(SHELL) $(top_builddir)/makes/build-script $? $@
