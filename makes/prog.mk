install: all prog-install post-install-hook
clean: prog-clean post-clean-hook
veryclean: prog-veryclean post-veryclean-hook
distclean: prog-distclean post-distclean-hook

post-install-hook:
post-clean-hook:
post-veryclean-hook:
post-distclean-hook:

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
