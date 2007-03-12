install: all lib-install post-install-hook
clean: lib-clean post-clean-hook
veryclean: lib-veryclean post-veryclean-hook
distclean: lib-distclean post-distclean-hook
gfregister: lib-gfregister post-gfregister-hook
man: lib-man post-man-hook
html: lib-html post-html-hook


post-install-hook:
post-clean-hook:
post-veryclean-hook:
post-distclean-hook:
post-gfregister-hook:
post-man-hook:
post-html-hook:

$(LIBRARY): $(OBJS) $(DEPLIBS)
	$(LTLINK) $(OBJS) $(LDLIBS)

lib-install:
	@$(MKDIR_P) $(DESTDIR)$(libdir)
	@for i in -- $(LIBRARY_RESULT); do \
		case $$i in --) continue;; esac; \
		echo \
		$(LTINSTALL_LIBRARY) $$i $(DESTDIR)$(libdir)/$$i; \
		$(LTINSTALL_LIBRARY) $$i $(DESTDIR)$(libdir)/$$i; \
	done

lib-clean:
	-$(LTCLEAN) $(OBJS) $(EXTRA_CLEAN_TARGETS)

lib-veryclean: clean
	-test -z "$(EXTRA_VERYCLEAN_TARGETS)" || $(LTCLEAN) $(EXTRA_VERYCLEAN_TARGETS)
	-test -z "$(LIBRARY)" || $(LTCLEAN) $(LIBRARY)
	-test -z "$(LIBRARY_RESULT)" || $(LTCLEAN) $(LIBRARY_RESULT)

lib-distclean: veryclean
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile

lib-gfregister:
lib-man:
lib-html:
