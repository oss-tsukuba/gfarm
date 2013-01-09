all: private-dir-tree-link subdir-all post-all-hook
install: subdir-install post-install-hook
clean: subdir-clean post-clean-hook
veryclean: subdir-veryclean post-very-clean-hook
distclean: subdir-distclean subdir-distclean-here post-distclean-hook
gfregister: subdir-gfregister post-gfregister-hook
man: subdir-man
html: subdir-html
msgno: subdir-msgno
catalog: subdir-catalog
include $(top_srcdir)/makes/private-subdir.mk

post-all-hook:
post-install-hook:
post-clean-hook:
post-very-clean-hook:
post-distclean-hook: private-dir-tree-remove
post-gfregister-hook:

# The reason why assignments of $(top_srcdir) and $(srcdir) are needed
# for Makefile.in case too is to prevent variable-inheritance caused by
# non Makefile.in case.

subdir-all subdir-install subdir-clean subdir-veryclean subdir-distclean subdir-gfregister subdir-man subdir-html subdir-msgno subdir-catalog:
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

subdir-distclean-here:
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile
