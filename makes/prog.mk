install: all prog-install post-install-hook
clean: prog-clean post-clean-hook
veryclean: prog-veryclean post-veryclean-hook
distclean: prog-distclean post-distclean-hook
gfregister: prog-gfregister post-gfregister-hook
man: prog-man post-man-hook
html: prog-html post-html-hook


post-install-hook:
post-clean-hook:
post-veryclean-hook:
post-distclean-hook:
post-gfregister-hook:
post-man-hook:
post-html-hook:

$(PROGRAM): $(OBJS) $(DEPLIBS)
	$(CC) -o $(PROGRAM) $(CFLAGS) $(OBJS) $(LDLIBS)

prog-install:
	$(INSTALL_PROGRAM) $(PROGRAM) $(bindir)/$(PROGRAM)

prog-clean:
	-rm -f $(OBJS) $(EXTRA_CLEAN_TARGETS)

prog-veryclean: clean
	-rm -f $(PROGRAM) $(EXTRA_VERYCLEAN_TARGETS)

prog-distclean: veryclean
	if [ -f $(srcdir)/Makefile.in ]; then rm -f Makefile; fi

prog-gfregister:
prog-man:
prog-html:
