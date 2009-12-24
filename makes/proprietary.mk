prop-link:
	if test "x$(PROPROOTDIR)" != "x"; then \
		tsDir=`(cd $(top_srcdir); /bin/pwd)` ; \
		crDir=`/bin/pwd` ; \
		tDir=`echo $${crDir} | sed "s:$${tsDir}::"` ; \
		$(RM) -f ./proprietary ; \
		ln -s $(PROPROOTDIR)$${tDir} ./proprietary ; \
	fi

prop-unlink:
	if test -L ./proprietary; then \
		$(RM) -f ./proprietary ; \
	fi
