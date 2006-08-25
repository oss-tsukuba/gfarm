all: data-all post-all-hook
install: all data-install post-install-hook
clean: data-clean post-clean-hook
veryclean: data-veryclean post-veryclean-hook
distclean: data-distclean post-distclean-hook
gfregister: data-gfregister post-gfregister-hook
man: data-man post-man-hook
html: data-html post-html-hook

post-all-hook:
post-install-hook:
post-clean-hook:
post-veryclean-hook:
post-distclean-hook:
post-gfregister-hook:
post-man-hook:
post-html-hook:

data-all:

data-install:
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

data-gfregister:
data-man:
data-html:
