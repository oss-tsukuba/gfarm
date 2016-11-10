all: post-all-hook
install: post-install-hook
clean: post-clean-hook
veryclean: post-veryclean-hook
distclean: post-distclean-hook
man: subdir-man
html: subdir-html
msgno: subdir-msgno
catalog: subdir-catalog
include $(top_srcdir)/makes/private-subdir.mk

post-all-hook: private-dir-tree-link subdir-all
post-install-hook: subdir-install
post-clean-hook: subdir-clean
post-veryclean-hook: subdir-veryclean
post-distclean-hook: private-dir-tree-remove subdir-distclean-here

# The reason why assignments of $(top_srcdir) and $(srcdir) are needed
# for Makefile.in case too is to prevent variable-inheritance caused by
# non Makefile.in case.

subdir-all subdir-install subdir-clean subdir-veryclean subdir-distclean subdir-man subdir-html subdir-msgno subdir-catalog:
	@target=`expr $@ : 'subdir-\(.*\)'`; \
	for dir in -- $(SUBDIRS); do \
		case $${dir} in --) continue;; esac; \
		echo '[' making $${dir} ']'; \
		case $(top_srcdir) in \
		/*)	top_srcdir=$(top_srcdir); \
			srcdir=$(srcdir)/$${dir};; \
		*)	rel=`echo $${dir}|sed 's|[^/][^/]*|..|g'`; \
			top_srcdir=$${rel}/$(top_srcdir); \
			srcdir=$${rel}/$(srcdir)/$${dir};; \
		esac; \
		if test -f $(srcdir)/$${dir}/Makefile.in; then \
			( cd $${dir} && \
			  case "$(srcdir)" in \
			  .)	$(MAKE) $${target};;\
			  *)	$(MAKE) top_srcdir=$${top_srcdir} \
					srcdir=$${srcdir} \
					$${target};; \
			  esac; \
			) || exit 1; \
		else \
			test -d $${dir} || mkdir -p $${dir} || exit 1; \
			( cd $${dir} && \
			  case "$(srcdir)" in \
			  .)	$(MAKE) $${target};;\
			  *)	$(MAKE) -f $${srcdir}/Makefile \
					top_srcdir=$${top_srcdir} \
					srcdir=$${srcdir} \
					$${target};; \
			  esac; \
			) || exit 1; \
		fi; \
	done

subdir-distclean-here: subdir-distclean
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile
