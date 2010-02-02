private_dir = ./private

private-dir-link:
	if test "x$(top_private_dir)" != "x"; then \
		tsDir=`(cd $(top_srcdir); /bin/pwd)` ; \
		crDir=`/bin/pwd` ; \
		tDir=`echo $${crDir} | sed "s:$${tsDir}::"` ; \
		$(RM) -f $(private_dir) ; \
		ln -s $(top_private_dir)$${tDir} $(private_dir) ; \
	fi

private-dir-unlink: private-src-unlink
	if test -L $(private_dir); then \
		$(RM) -f $(private_dir) ; \
	fi

private-src-link: private-dir-link
	if test "x$(PRIVATE_FILES)" != "x"; then \
		for i in $(PRIVATE_FILES); do \
			if test ! -r ./$${i}; then \
				if test -r $(private_dir)/$${i}; then \
					ln -s $(private_dir)/$${i} . ; \
				fi ; \
			fi ; \
		done ; \
	fi

private-src-unlink:
	if test "x$(PRIVATE_FILES)" != "x"; then \
		for i in $(PRIVATE_FILES); do \
			if test -L ./$${i}; then \
				$(RM) ./$${i} ; \
			fi ; \
		done ; \
	fi

private-dir-tree-link:
	if test "x${top_private_dir}" != "x"; then \
		tsDir=`(cd $(top_srcdir); /bin/pwd)` ; \
		crDir=`/bin/pwd` ; \
		tDir=`echo $${crDir} | sed "s:$${tsDir}::"` ; \
		if test "x${PRIVATE_TARGETS}" != "x"; then \
			for i in ${PRIVATE_TARGETS}; do \
				if test ! -d ./$${i}; then \
					mkdir ./$${i}; \
				fi; \
				lndir ${top_private_dir}$${tDir}/$${i} "`pwd`"/$${i}; \
			done ; \
		fi; \
	fi

private-dir-tree-remove:
	if test "x${top_private_dir}" != "x"; then \
		if test "x${PRIVATE_TARGETS}" != "x"; then \
			for i in ${PRIVATE_TARGETS}; do \
				if test -d ./$${i}; then \
					$(RM) -rf ./$${i}; \
				fi ; \
			done ; \
		fi ; \
	fi

private-initialize:	private-src-link
private-finalize:	private-dir-unlink
