all: catalog-all post-all-hook
install: catalog-install post-install-hook
clean: catalog-clean post-clean-hook
veryclean: catalog-veryclean post-very-clean-hook
distclean: catalog-distclean post-distclean-hook
gfregister: catalog-gfregister post-gfregister-hook
man: gflog-man
html: gflog-html
msgno: catalog-msgno
catalog: catalog-catalog

post-all-hook:
post-install-hook:
post-clean-hook:
post-very-clean-hook:
post-distclean-hook:
post-gfregister-hook:

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

catalog-gfregister:
catalog-man:
catalog-html:
catalog-msgno:
catalog-catalog: $(CATALOG_FILES)
