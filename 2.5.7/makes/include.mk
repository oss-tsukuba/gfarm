all: include-all post-all-hook
install: all include-install post-install-hook
clean: include-clean post-clean-hook
veryclean: include-veryclean post-veryclean-hook
distclean: include-distclean post-distclean-hook
gfregister: include-gfregister post-gfregister-hook
man: include-man post-man-hook
html: include-html post-html-hook
msgno: include-msgno
catalog: include-catalog


post-all-hook:
post-install-hook:
post-clean-hook:
post-veryclean-hook:
post-distclean-hook:
post-gfregister-hook:
post-man-hook:
post-html-hook:

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

include-gfregister:
include-man:
include-html:
include-msgno:
include-catalog:
