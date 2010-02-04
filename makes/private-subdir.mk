private-dir-tree-link:
	@if test "x${top_private_dir}" != "x"; then \
		tsDir=`(cd $(top_srcdir); /bin/pwd)` ; \
		crDir=`/bin/pwd` ; \
		tDir=`echo $${crDir} | sed "s:$${tsDir}::"` ; \
		if test "x${PRIVATE_TARGETS}" != "x"; then \
			for i in ${PRIVATE_TARGETS}; do \
				if test ! -d ./$${i}; then \
					mkdir ./$${i}; \
				fi; \
				lndir ${top_private_dir}$${tDir}/$${i} "`/bin/pwd`"/$${i}; \
			done ; \
		fi; \
	fi

private-dir-tree-remove:
	@if test "x${top_private_dir}" != "x"; then \
		if test "x${PRIVATE_TARGETS}" != "x"; then \
			for i in ${PRIVATE_TARGETS}; do \
				if test -d ./$${i}; then \
					$(RM) -rf ./$${i}; \
				fi ; \
			done ; \
		fi ; \
	fi

