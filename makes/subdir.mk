all: subdir-all post-all-hook
install: subdir-install post-install-hook
clean: subdir-clean post-clean-hook
veryclean: subdir-veryclean post-very-clean-hook
distclean: subdir-distclean post-distclean-hook
gfregister: subdir-gfregister post-gfregister-hook
man: subdir-man
html: subdir-html

post-all-hook:
post-install-hook:
post-clean-hook:
post-very-clean-hook:
post-distclean-hook:
post-gfregister-hook:

subdir-all subdir-install subdir-clean subdir-veryclean subdir-distclean subdir-gfregister subdir-man subdir-html:
	@target=`expr $@ : 'subdir-\(.*\)'`; \
	for dir in / $(SUBDIRS); do \
		case $${dir} in /) continue;; esac; \
		echo '[' making $${dir} ']'; \
		if test -f $(srcdir)/$${dir}/Makefile.in; then \
			( cd $${dir} && \
			  case "$(srcdir)" in \
			  .)	$(MAKE) $${target};; \
			  *)	$(MAKE) \
					top_srcdir=$(top_srcdir) \
					top_objdir=$(top_objdir) \
					srcdir=$(srcdir)/$${dir} \
					VPATH='$$(srcdir)' \
					$${target};; \
			  esac; \
			) || exit 1; \
		else \
			test -d $${dir} || mkdir -p $${dir} || exit 1; \
			( cd $${dir} && \
			  case "$(srcdir)" in \
			  .)	$(MAKE) $${target};; \
			  *)	$(MAKE) -f $(srcdir)/$${dir}/Makefile \
					top_srcdir=$(top_srcdir) \
					top_objdir=$(top_objdir) \
					srcdir=$(srcdir)/$${dir} \
					VPATH='$$(srcdir)' \
					$${target};; \
			  esac; \
			) || exit 1; \
		fi; \
	done
