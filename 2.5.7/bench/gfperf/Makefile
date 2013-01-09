top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .
gfperfdir = /gfperf

include $(top_srcdir)/makes/var.mk

SUBDIRS = \
	gfperf-lib \
	gfperf-metadata	\
	gfperf-copy \
	gfperf-replica \
	gfperf-tree \
	gfperf-autoreplica \
	gfperf-read \
	gfperf-write \
	gfperf-parallel-read \
	gfperf-parallel-write \
	gfperf-parallel-replica \
	gfperf-parallel-autoreplica \
	gfperf-rb \
	gfperf-wrapper \
	gfperf-web \
	gfstress

include $(top_srcdir)/makes/subdir.mk

DOC = CONFIG-gfperf.en \
      README-gfperf.en \
      SETUP-gfperf.en \
      SUPPORT-gfperf.en \
      USING-gfperf.en \
      CONFIG-gfperf.ja \
      README-gfperf.ja \
      SETUP-gfperf.ja \
      SUPPORT-gfperf.ja \
      USING-gfperf.ja

install:
	@$(MKDIR_P) $(DESTDIR)$(docdir)$(gfperfdir)
	@for i in -- $(DOC); do \
		case $$i in --) continue;; esac; \
		echo \
		$(INSTALL_DOC) $(srcdir)/$${i} \
			$(DESTDIR)$(docdir)$(gfperfdir)/`basename $${i}`; \
		$(INSTALL_DOC) $(srcdir)/$${i} \
			$(DESTDIR)$(docdir)$(gfperfdir)/`basename $${i}`; \
	done
