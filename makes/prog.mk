install: post-install-hook
clean: post-clean-hook
veryclean: post-veryclean-hook
distclean: post-distclean-hook
man: post-man-hook
html: post-html-hook
msgno: prog-msgno
catalog: prog-catalog

include $(top_srcdir)/makes/private-file.mk

post-install-hook: prog-install
post-clean-hook: prog-clean
post-veryclean-hook: prog-veryclean
post-distclean-hook: prog-distclean
post-man-hook: prog-man
post-html-hook: prog-html

$(PROGRAM): $(OBJS) $(DEPLIBS)
	$(LTLINK) $(OBJS) $(LDLIBS)

prog-install: all
	@$(MKDIR_P) $(DESTDIR)$(bindir)
	$(LTINSTALL_PROGRAM) $(PROGRAM) $(DESTDIR)$(bindir)/$(PROGRAM)

prog-clean:
	-$(LTCLEAN) $(OBJS) $(EXTRA_CLEAN_TARGETS)

prog-veryclean: clean private-finalize
	-$(LTCLEAN) $(PROGRAM) $(EXTRA_VERYCLEAN_TARGETS)

prog-distclean: veryclean
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile

prog-man:
prog-html:
prog-msgno:
prog-catalog:
$(PRIVATE_FILES): private-initialize
