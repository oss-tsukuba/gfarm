$(PROGRAM): $(OBJS) $(DEPLIBS)
	$(CC) -o $(PROGRAM) $(CFLAGS) $(OBJS) $(LDLIBS)

install-program:
	if [ ! -d $(bindir) ]; then \
		mkdir -p $(bindir); \
	fi
	cp -p $(PROGRAM) $(bindir)

install: all pre-install-hook install-program post-install-hook

clean:
	-rm -f $(OBJS) $(EXTRA_CLEAN_TARGETS)

veryclean: clean
	-rm -f $(PROGRAM) $(EXTRA_VERYCLEAN_TARGETS)

distclean: veryclean
	if [ -f Makefile.in ]; then rm -f Makefile; fi

pre-install-hook:
post-install-hook:
