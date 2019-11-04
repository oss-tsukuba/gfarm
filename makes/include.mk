all: post-all-hook
install: post-install-hook
clean: post-clean-hook
veryclean: post-veryclean-hook
distclean: post-distclean-hook
man: post-man-hook
html: post-html-hook
msgno: include-msgno
catalog: include-catalog


post-all-hook: include-all
post-install-hook: include-install
post-clean-hook: include-clean
post-veryclean-hook: include-veryclean
post-distclean-hook: include-distclean
post-man-hook: include-man
post-html-hook: include-html

include-all:
include-install: all
	@$(MKDIR_P) $(DESTDIR)$(includedir) $(DESTDIR)$(exec_includedir)
	@for i in -- $(INCS); do \
		case $$i in --) continue;; esac; \
		echo \
		$(INSTALL_DATA) $(srcdir)/$$i $(DESTDIR)$(includedir)/$$i; \
		$(INSTALL_DATA) $(srcdir)/$$i $(DESTDIR)$(includedir)/$$i; \
	done
	@for i in -- $(EXEC_INCS); do \
		case $$i in --) continue;; esac; \
		echo \
		$(INSTALL_DATA) $$i $(DESTDIR)$(exec_includedir)/$$i; \
		$(INSTALL_DATA) $$i $(DESTDIR)$(exec_includedir)/$$i; \
	done

include-clean:
	-test -z "$(EXTRA_CLEAN_TARGETS)" || $(RM) -f $(EXTRA_CLEAN_TARGETS)

include-veryclean: clean
	-test -z "$(EXTRA_VERYCLEAN_TARGETS)" || $(RM) -f $(EXTRA_VERYCLEAN_TARGETS)

include-distclean: veryclean
	-test -z "$(EXEC_INCS)" || $(RM) -f $(EXEC_INCS)
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile

include-man:
include-html:
include-msgno:
include-catalog:
