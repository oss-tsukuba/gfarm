private-dir-link:
	@if test "x$(top_private_dir)" != "x"; then \
		tsDir=`(cd $(top_srcdir); /bin/pwd)` ; \
		crDir=`/bin/pwd` ; \
		tDir=`echo $${crDir} | sed "s:$${tsDir}::"` ; \
		$(RM) -f $(private_dir) ; \
		ln -s $(top_private_dir)$${tDir} $(private_dir) ; \
	fi

private-dir-unlink: private-src-unlink
	@if test -L $(private_dir); then \
		$(RM) -f $(private_dir) ; \
	fi

private-src-link: private-dir-link
	for i in -- $(PRIVATE_FILES); do \
		case $$i in --) continue;; esac; \
		if test ! -r ./$${i}; then \
			if test -r $(private_dir)/$${i}; then \
				ln -s $(private_dir)/$${i} . ; \
			fi ; \
		fi ; \
	done

private-src-unlink:
	for i in -- $(PRIVATE_FILES); do \
		case $$i in --) continue;; esac; \
		if test -L ./$${i}; then \
			$(RM) ./$${i} ; \
		fi ; \
	done

private-initialize:	private-src-link
private-finalize:	private-dir-unlink
