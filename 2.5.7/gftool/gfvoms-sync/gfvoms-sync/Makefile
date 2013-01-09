top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

datadir = $(default_datadir)/gfarm/voms

SCRIPTS = gfvoms-sync
DATA = $(srcdir)/gfvoms-sync.py
EXTRA_CLEAN_TARGETS = $(SCRIPTS)

all: $(SCRIPTS)

$(SCRIPTS):
	./mkscript $(datadir)

include $(top_srcdir)/makes/script.mk
include $(top_srcdir)/makes/data.mk
