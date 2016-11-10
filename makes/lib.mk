install: post-install-hook
clean: post-clean-hook
veryclean: post-veryclean-hook
distclean: post-distclean-hook
man: post-man-hook
html: post-html-hook
msgno: lib-msgno

include $(top_srcdir)/makes/private-file.mk

post-install-hook: lib-install
post-clean-hook: lib-clean
post-veryclean-hook: lib-veryclean
post-distclean-hook: lib-distclean
post-man-hook: lib-man
post-html-hook: lib-html

$(LIBRARY): $(OBJS) $(DEPLIBS)
	$(LTLINK) $(OBJS) $(LDLIBS)

lib-install: all
	@$(MKDIR_P) $(DESTDIR)$(libdir)
	@for i in -- $(LIBRARY_RESULT); do \
		case $$i in --) continue;; esac; \
		echo \
		$(LTINSTALL_LIBRARY) $$i $(DESTDIR)$(libdir)/$$i; \
		$(LTINSTALL_LIBRARY) $$i $(DESTDIR)$(libdir)/$$i; \
	done

lib-clean:
	-$(LTCLEAN) $(OBJS) $(EXTRA_CLEAN_TARGETS)

lib-veryclean: clean private-finalize
	-test -z "$(EXTRA_VERYCLEAN_TARGETS)" || $(LTCLEAN) $(EXTRA_VERYCLEAN_TARGETS)
	-test -z "$(LIBRARY)" || $(LTCLEAN) $(LIBRARY)
	-test -z "$(LIBRARY_RESULT)" || $(LTCLEAN) $(LIBRARY_RESULT)


lib-distclean: veryclean
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile

lib-man:
lib-html:
lib-msgno:

$(PRIVATE_FILES): private-initialize
