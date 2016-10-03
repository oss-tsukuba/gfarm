all: post-all-hook
install: post-install-hook
clean: post-clean-hook
veryclean: post-veryclean-hook
distclean: post-distclean-hook
man: post-man-hook
html: post-html-hook
msgno: script-msgno
catalog: script-catalog

include $(top_srcdir)/makes/private-file.mk

post-all-hook: script-all
post-install-hook: script-install
post-clean-hook: script-clean
post-veryclean-hook: script-veryclean
post-distclean-hook: script-distclean
post-man-hook: script-man
post-html-hook: script-html

script-all: $(BUILT_SCRIPTS)

script-install: all
	@$(MKDIR_P) $(DESTDIR)$(bindir)
	@for i in -- $(SCRIPTS) $(BUILT_SCRIPTS); do \
		case $$i in --) continue;; esac; \
		echo \
		$(INSTALL_SCRIPT) $$i $(DESTDIR)$(bindir)/`basename $$i`; \
		$(INSTALL_SCRIPT) $$i $(DESTDIR)$(bindir)/`basename $$i`; \
	done

script-clean:
	-test -z "$(EXTRA_CLEAN_TARGETS)" || $(RM) -f $(EXTRA_CLEAN_TARGETS)
	-test -z "$(BUILT_SCRIPTS)" || $(RM) -f $(BUILT_SCRIPTS)

script-veryclean: clean private-finalize
	-test -z "$(EXTRA_VERYCLEAN_TARGETS)" || $(RM) -f $(EXTRA_VERYCLEAN_TARGETS)

script-distclean: veryclean
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile

script-man:
script-html:
script-msgno:
script-catalog:

$(PRIVATE_FILES): private-initialize
