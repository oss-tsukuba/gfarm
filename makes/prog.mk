install: all prog-install post-install-hook
clean: prog-clean post-clean-hook
veryclean: prog-veryclean post-veryclean-hook
distclean: prog-distclean post-distclean-hook
gfregister: prog-gfregister post-gfregister-hook
man: prog-man post-man-hook
html: prog-html post-html-hook
msgno: prog-msgno
catalog: prog-catalog

include $(top_srcdir)/makes/private-file.mk

post-install-hook:
post-clean-hook:
post-veryclean-hook:
post-distclean-hook:
post-gfregister-hook:
post-man-hook:
post-html-hook:

$(PROGRAM): $(OBJS) $(DEPLIBS)
	$(LTLINK) $(OBJS) $(LDLIBS)

prog-install:
	@$(MKDIR_P) $(DESTDIR)$(bindir)
	$(LTINSTALL_PROGRAM) $(PROGRAM) $(DESTDIR)$(bindir)/$(PROGRAM)

prog-clean:
	-$(LTCLEAN) $(OBJS) $(EXTRA_CLEAN_TARGETS)

prog-veryclean: clean private-finalize
	-$(LTCLEAN) $(PROGRAM) $(EXTRA_VERYCLEAN_TARGETS)

prog-distclean: veryclean
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile

prog-gfregister:
prog-man:
prog-html:
prog-msgno:
prog-catalog:
$(PRIVATE_FILES): private-initialize
