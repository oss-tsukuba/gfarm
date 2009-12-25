PROPDIR = ./proprietary

prop-dir-link:
	if test "x$(PROPROOTDIR)" != "x"; then \
		tsDir=`(cd $(top_srcdir); /bin/pwd)` ; \
		crDir=`/bin/pwd` ; \
		tDir=`echo $${crDir} | sed "s:$${tsDir}::"` ; \
		$(RM) -f $(PROPDIR) ; \
		ln -s $(PROPROOTDIR)$${tDir} $(PROPDIR) ; \
	fi

prop-dir-unlink: prop-src-unlink
	if test -L $(PROPDIR); then \
		$(RM) -f $(PROPDIR) ; \
	fi

prop-src-link: prop-dir-link
	if test "x$(PROPRIETARY_SRCS)" != "x"; then \
		for i in $(PROPRIETARY_SRCS); do \
			if test ! -r ./$${i}; then \
				if test -r $(PROPDIR)/$${i}; then \
					ln -s $(PROPDIR)/$${i} . ; \
				fi ; \
			fi ; \
		done ; \
	fi

prop-src-unlink:
	if test "x$(PROPRIETARY_SRCS)" != "x"; then \
		for i in $(PROPRIETARY_SRCS); do \
			if test -L ./$${i}; then \
				$(RM) ./$${i} ; \
			fi ; \
		done ; \
	fi

prop-initialize:	prop-src-link
prop-finalize:		prop-dir-unlink
