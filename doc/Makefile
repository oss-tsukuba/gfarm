top_objdir = ..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SUBDIRS = html

DOC =	../INSTALL.en ../INSTALL.ja ../LICENSE \
	GUIDE.ja Gfarm-FAQ.en Gfarm-FAQ.ja README.hook.en

include $(top_srcdir)/makes/subdir.mk

install:
	@for i in / $(DOC); do \
		case $$i in /) continue;; esac; \
		( set -x; \
		  $(INSTALL_DATA) $(srcdir)/$${i} $(docdir)/`basename $${i}` \
		); \
	done
