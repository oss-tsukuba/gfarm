all: subdir-all post-all-hook
install: subdir-install post-install-hook
clean: subdir-clean post-clean-hook
veryclean: subdir-veryclean post-very-clean-hook
distclean: subdir-distclean post-distclean-hook

post-all-hook:
post-install-hook:
post-clean-hook:
post-very-clean-hook:
post-distclean-hook:

subdir-all subdir-install subdir-clean subdir-veryclean subdir-distclean:
	@target=`expr $@ : 'subdir-\(.*\)'`; \
	for dir in / $(SUBDIRS); do \
		case $${dir} in /) continue;; esac; \
		echo '[' making $${dir} ']'; \
		if test -f $(srcdir)/$${dir}/Makefile.in; then \
			( cd $${dir} && \
			  if [ "$(srcdir)" = "." ]; then \
				$(MAKE) $${target}; \
			  else \
				$(MAKE) \
					top_srcdir=$(top_srcdir) \
					top_objdir=$(top_objdir) \
					srcdir=$(srcdir)/$${dir} \
					VPATH='$$(srcdir)' \
					$${target}; \
			  fi; \
			) || exit 1; \
		else \
			test -d $${dir} || mkdir $${dir} || exit 1; \
			( cd $${dir} && \
			  if [ "$(srcdir)" = "." ]; then \
				$(MAKE) $${target}; \
			  else \
				$(MAKE) -f $(srcdir)/$${dir}/Makefile \
					top_srcdir=$(top_srcdir) \
					top_objdir=$(top_objdir) \
					srcdir=$(srcdir)/$${dir} \
					VPATH='$$(srcdir)' \
					$${target}; \
			  fi; \
			) || exit 1; \
		fi; \
	done
