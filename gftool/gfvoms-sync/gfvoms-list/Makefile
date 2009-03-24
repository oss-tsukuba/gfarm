top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

all: script-all
install: all script-install
clean: script-clean
veryclean: script-veryclean
distclean: script-distclean
gfregister: script-gfregister
man: script-man
html: script-html

SCRIPTS = $(srcdir)/gfvoms-list

script-install:
	@$(MKDIR_P) $(bindir)
	@for i in -- $(SCRIPTS); do \
		case $$i in --) continue;; esac; \
		echo \
		$(INSTALL_SCRIPT) $$i $(bindir)/`basename $$i`; \
		$(INSTALL_SCRIPT) $$i $(bindir)/`basename $$i`; \
	done

script-clean:
	-test -z "$(EXTRA_CLEAN_TARGETS)" || $(RM) -f $(EXTRA_CLEAN_TARGETS)

script-veryclean: clean
	-test -z "$(EXTRA_VERYCLEAN_TARGETS)" || $(RM) -f "$(EXTRA_VERYCLEAN_TARGETS)"

script-distclean: veryclean
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile

script-all:
script-gfregister:
script-man:
script-html:
