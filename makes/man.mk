all: man-all post-all-hook
install: man-install post-install-hook
clean: man-clean post-clean-hook
veryclean: man-veryclean post-very-clean-hook
distclean: man-distclean post-distclean-hook
gfregister: man-gfregister post-gfregister-hook
man: man-man
html: man-html

post-all-hook:
post-install-hook:
post-clean-hook:
post-very-clean-hook:
post-distclean-hook:
post-gfregister-hook:

man-all:

man-install:
	@for i in / $(MAN); do \
		case $$i in /) continue;; esac; \
		suffix=`expr $$i : '.*\.\([^.]*\)$$'`; \
		( set -x; $(INSTALL_DATA) $$i $(mandir)/man$$suffix/$$i ); \
	done

man-clean:
	-rm -f $(EXTRA_CLEAN_TARGETS)

man-veryclean: clean
	-rm -f $(EXTRA_VERYCLEAN_TARGETS)

man-distclean: veryclean
	if [ -f $(srcdir)/Makefile.in ]; then rm -f Makefile; fi

man-gfregister:

$(dstsubst): $(srcsubst)
	$(DOCBOOK2MAN) $(srcsubst)

man-man:
	for i in $(DOCBOOK); do \
		$(MAKE) srcsubst=$(DOCBOOK_DIR)/$${i}.docbook \
			dstsubst=$$i $$i; \
	done

man-html:
