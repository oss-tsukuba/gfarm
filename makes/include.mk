all: include-all post-all-hook
install: all include-install post-install-hook
clean: include-clean post-clean-hook
veryclean: include-veryclean post-veryclean-hook
distclean: include-distclean post-distclean-hook
gfregister: include-gfregister post-gfregister-hook
man: include-man post-man-hook
html: include-html post-html-hook


post-all-hook:
post-install-hook:
post-clean-hook:
post-veryclean-hook:
post-distclean-hook:
post-gfregister-hook:
post-man-hook:
post-html-hook:

include-all:
include-install: all
	( cd $(srcdir) && \
	  for i in *.h; do $(INSTALL_DATA) $$i $(includedir)/$$i; done )
	@set -x; \
	for i in / $(EXEC_INCS); do \
		case $$i in /) continue;; esac; \
		$(INSTALL_DATA) $$i $(exec_includedir)/$$i; \
	done

include-clean:

include-veryclean: clean
	-rm -f $(EXECHEADERS)

include-distclean: veryclean

include-gfregister:
include-man:
include-html:
