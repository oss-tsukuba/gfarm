top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .
vomslib = $(prefix)/share/gfarm/voms
confpath = $(sysconfdir)

vomsids_name = "gfarm.vomsids"
hostidpy_name = "hostid.py"

include $(top_srcdir)/makes/var.mk

all: script-all
install: all script-install
clean: script-clean
veryclean: script-veryclean
distclean: script-distclean
gfregister: script-gfregister
man: script-man
html: script-html

SCRIPTS = zsi_def.py $(hostidpy_name)

script-all:
	@./mkscript $(hostidpy_name) $(confpath) $(vomsids_name);

script-install:
	@$(MKDIR_P) $(vomslib)
	@for i in -- $(SCRIPTS); do \
		case $$i in --) continue;; esac; \
		echo \
		$(INSTALL_DATA) $$i $(vomslib)/`basename $$i`; \
		$(INSTALL_DATA) $$i $(vomslib)/`basename $$i`; \
	done
	@echo $(INSTALL_DATA) $(srcdir)/$(vomsids_name) $(confpath)/`basename $(vomsids_name)`
	@$(INSTALL_DATA) $(srcdir)/$(vomsids_name) $(confpath)/`basename $(vomsids_name)`
	

script-clean:
	@for i in -- ${vomsids_name} ${hostidpy_name}; do \
		 test -e $$i && $(RM) -f $$i; \
	done

script-veryclean: clean
	-test -z "$(EXTRA_VERYCLEAN_TARGETS)" || $(RM) -f $(EXTRA_VERYCLEAN_TARGETS)

script-distclean: veryclean
	-test ! -f $(srcdir)/Makefile.in || $(RM) -f Makefile

script-gfregister:
script-man:
script-html:
