all: post-all-hook
install: post-install-hook
clean: post-clean-hook
veryclean: post-veryclean-hook
distclean: post-distclean-hook
man: gflog-man
html: gflog-html
msgno: catalog-msgno
catalog: catalog-catalog

post-all-hook: catalog-all
post-install-hook: catalog-install
post-clean-hook: catalog-clean
post-veryclean-hook: catalog-veryclean
post-distclean-hook: catalog-distclean

catalog-all: catalog-catalog
catalog-install:
	@$(MKDIR_P) $(DESTDIR)$(localedir)/$(LOCALE)
	@for i in -- $(CATALOG_FILES); do \
		case $$i in --) continue;; esac; \
		echo \
		$(INSTALL_DATA) $$i $(DESTDIR)$(localedir)/$(LOCALE); \
		$(INSTALL_DATA) $$i $(DESTDIR)$(localedir)/$(LOCALE); \
	done

catalog-clean:
	-test -z "$(EXTRA_CLEAN_TARGETS)" || $(RM) -f $(EXTRA_CLEAN_TARGETS)
catalog-veryclean: clean
	-test -z "$(EXTRA_VERYCLEAN_TARGETS)" || $(RM) -f $(EXTRA_VERYCLEAN_TARGETS)
catalog-distclean: veryclean
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile

catalog-man:
catalog-html:
catalog-msgno:
catalog-catalog: $(CATALOG_FILES)
