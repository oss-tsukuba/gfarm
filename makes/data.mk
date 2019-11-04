all: post-all-hook
install: post-install-hook
clean: post-clean-hook
veryclean: post-veryclean-hook
distclean: post-distclean-hook
man: post-man-hook
html: post-html-hook
msgno: data-msgno
catalog: data-catalog

post-all-hook: data-all
post-install-hook: data-install
post-clean-hook: data-clean
post-veryclean-hook: data-veryclean
post-distclean-hook: data-distclean
post-man-hook: data-man
post-html-hook: data-html

data-all:

data-install: all
	@$(MKDIR_P) $(DESTDIR)$(datadir)
	@for i in -- $(DATA); do \
		case $$i in --) continue;; esac; \
		echo \
		$(INSTALL_DATA) $$i $(DESTDIR)$(datadir)/`basename $$i`; \
		$(INSTALL_DATA) $$i $(DESTDIR)$(datadir)/`basename $$i`; \
	done

data-clean:
	-test -z "$(EXTRA_CLEAN_TARGETS)" || $(RM) -f $(EXTRA_CLEAN_TARGETS)

data-veryclean: clean
	-test -z "$(EXTRA_VERYCLEAN_TARGETS)" || $(RM) -f $(EXTRA_VERYCLEAN_TARGETS)

data-distclean: veryclean
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile

data-man:
data-html:
data-msgno:
data-catalog:
