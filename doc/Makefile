top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SUBDIRS = html

DOC =	../LICENSE ../RELNOTES \
	../README.en ../README.ja ../OVERVIEW.en ../OVERVIEW.ja \
	../INSTALL.en ../INSTALL.ja  \
	../INSTALL.RPM.en ../INSTALL.RPM.ja \
	../INSTALL.Debian.en ../INSTALL.Debian.ja \
	../SETUP.en ../SETUP.ja SETUP.private.en SETUP.private.ja \
	DEVELOPER \
	Gfarm-FAQ.en Gfarm-FAQ.ja \
	KNOWN_PROBLEMS.en KNOWN_PROBLEMS.ja \
	quota.en \
	iostat-dev.ja iostat-setup.ja \
	voms-dev.ja voms-install.ja \
	xattr-dev.ja xattr-install.ja xattr-man.ja \
	failover.ja disaster.ja \
	encfs.en.md encfs.ja.md \
	automount.en.md automount.ja.md

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
