top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SUBDIRS = html

DOC =	../INSTALL.en ../INSTALL.ja ../LICENSE ../RELNOTES \
	../INSTALL.RPM.en ../INSTALL.RPM.ja \
	GUIDE.ja Gfarm-FAQ.en Gfarm-FAQ.ja README.hook.en README.hook.ja

include $(top_srcdir)/makes/subdir.mk

install:
	@$(MKDIR_P) $(DESTDIR)$(docdir)
	@for i in -- $(DOC); do \
		case $$i in --) continue;; esac; \
		echo \
		$(INSTALL_DOC) $(srcdir)/$${i} \
			$(DESTDIR)$(docdir)/`basename $${i}`; \
		$(INSTALL_DOC) $(srcdir)/$${i} \
			$(DESTDIR)$(docdir)/`basename $${i}`; \
	done
