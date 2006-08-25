all: html-all post-all-hook
install: html-install post-install-hook
clean: html-clean post-clean-hook
veryclean: html-veryclean post-very-clean-hook
distclean: html-distclean post-distclean-hook
gfregister: html-gfregister post-gfregister-hook
man: html-man
html: html-html

post-all-hook:
post-install-hook:
post-clean-hook:
post-very-clean-hook:
post-distclean-hook:
post-gfregister-hook:

html-all:

html-install:
	@$(MKDIR_P) $(DESTDIR)$(htmldir)
	@for i in -- $(HTML); do \
		case $$i in --) continue;; esac; \
		echo \
		$(INSTALL_DOC) $(srcdir)/$${i} $(DESTDIR)$(htmldir)/$${i}; \
		$(INSTALL_DOC) $(srcdir)/$${i} $(DESTDIR)$(htmldir)/$${i}; \
	done
	@for i in -- $(HTMLSRC); do \
		case $$i in --) continue;; esac; \
		echo \
		$(INSTALL_DOC) $(srcdir)/$${i}.html \
			$(DESTDIR)$(htmldir)/$${i}.html; \
		$(INSTALL_DOC) $(srcdir)/$${i}.html \
			$(DESTDIR)$(htmldir)/$${i}.html; \
	done

html-clean:
	-test -z "$(EXTRA_CLEAN_TARGETS)" || $(RM) -f $(EXTRA_CLEAN_TARGETS)

html-veryclean: clean
	-test -z "$(EXTRA_VERYCLEAN_TARGETS)" || $(RM) -f $(EXTRA_VERYCLEAN_TARGETS)

html-distclean: veryclean
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile

html-gfregister:
html-man:

$(dstsubst): $(srcsubst)
	$(DOCBOOK2HTML) $(srcsubst)

html-html:
	for i in -- $(HTMLSRC); do \
		case $$i in --) continue;; esac; \
		$(MAKE) srcsubst=$(DOCBOOK_DIR)/$${i}.docbook \
			dstsubst=$${i}.html $${i}.html; \
	done
