all: script-all post-all-hook
install: all script-install post-install-hook
clean: script-clean post-clean-hook
veryclean: script-veryclean post-veryclean-hook
distclean: script-distclean post-distclean-hook
gfregister: script-gfregister post-gfregister-hook
man: script-man post-man-hook
html: script-html post-html-hook

post-all-hook:
post-install-hook:
post-clean-hook:
post-veryclean-hook:
post-distclean-hook:
post-gfregister-hook:
post-man-hook:
post-html-hook:

script-all:

script-install:
	@$(MKDIR_P) $(DESTDIR)$(bindir)
	@for i in -- $(SCRIPTS); do \
		case $$i in --) continue;; esac; \
		echo \
		$(INSTALL_SCRIPT) $$i $(DESTDIR)$(bindir)/`basename $$i`; \
		$(INSTALL_SCRIPT) $$i $(DESTDIR)$(bindir)/`basename $$i`; \
	done

script-clean:
	-test -z "$(EXTRA_CLEAN_TARGETS)" || $(RM) -f $(EXTRA_CLEAN_TARGETS)

script-veryclean: clean
	-test -z "$(EXTRA_VERYCLEAN_TARGETS)" || $(RM) -f $(EXTRA_VERYCLEAN_TARGETS)

script-distclean: veryclean
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile

script-gfregister:
script-man:
script-html:
