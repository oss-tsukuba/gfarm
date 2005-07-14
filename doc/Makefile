top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SUBDIRS = html

DOC =	../INSTALL.en ../INSTALL.ja ../LICENSE ../RELNOTES \
	../INSTALL.RPM.en ../INSTALL.RPM.ja \
	GUIDE.ja Gfarm-FAQ.en Gfarm-FAQ.ja README.hook.en

include $(top_srcdir)/makes/subdir.mk

install:
	@$(MKDIR_P) $(DESTDIR)$(docdir)
	@for i in -- $(DOC); do \
		case $$i in --) continue;; esac; \
		( set -x; \
		  $(INSTALL_DATA) $(srcdir)/$${i} \
			$(DESTDIR)$(docdir)/`basename $${i}` \
		); \
	done
