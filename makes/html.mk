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
	@for i in / $(DOCBOOK); do \
		n=`basename $$i .docbook`.html; \
		case $$i in /) continue;; esac; \
		( set -x; $(INSTALL_DATA) $$n $(htmldir)/$$n ); \
	done

html-clean:
	-rm -f $(EXTRA_CLEAN_TARGETS)

html-veryclean: clean
	-rm -f $(EXTRA_VERYCLEAN_TARGETS)

html-distclean: veryclean
	if [ -f $(srcdir)/Makefile.in ]; then rm -f Makefile; fi

html-gfregister:
html-man:

$(dstsubst): $(srcsubst)
	$(DOCBOOK2HTML) $(srcsubst)

html-html:
	for i in $(DOCBOOK); do \
		$(MAKE) srcsubst=$(DOCBOOK_DIR)/$${i}.docbook \
			dstsubst=$${i}.html $${i}.html; \
	done
