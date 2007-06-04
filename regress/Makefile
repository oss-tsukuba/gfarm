top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

POSTGRESQL_TARGETS=postgresql
LDAP_TARGETS=ldap
backend_targets=$(postgresql_targets) $(ldap_targets)

GLOBUS_TARGETS= gsi gsi_auth
auth_targets=	sharedsecret $(globus_targets)

agent_targets=	disable enable only

CONFIG_PREFIX=	/var/tmp/`whoami`
CONFIG_OPT=	# -k
REGRESS_SH=		$(srcdir)/regress.sh
TEST_TARGET=		$(srcdir)/check.sh --prefix $(prefix)
LOG=			log

# These environment settings aren't enough,
# and .bashrc, .zshenv or .cshrc must be modified to set those environments,
# otherwise gfrcmd and gfrun may not work correctly.
# But these settings save tests other than those.
CHECK_ENV=	LANG=C PATH=$(prefix)/bin:$$PATH
SELFCHECK_ENV=	LANG=C

# subdirectories which have to be built
SUBDIRS=	\
	lib/libgfarm/gfarm/gfarm_initialize \
	lib/libgfarm/gfarm/gfs_pio_set_local \
	lib/libgfarm/gfarm/gfs_pio_create \
	lib/libgfarm/gfarm/gfs_pio_open \
	lib/libgfarm/gfarm/gfs_pio_close \
	lib/libgfarm/gfarm/gfs_pio_set_view_index \
	lib/libgfarm/gfarm/gfs_pio_set_view_local \
	lib/libgfarm/gfarm/gfs_pio_error \
	lib/libgfarm/gfarm/gfs_pio_eof \
	lib/libgfarm/gfarm/gfs_pio_seek \
	lib/libgfarm/gfarm/gfs_pio_getc \
	lib/libgfarm/gfarm/gfs_pio_ungetc \
	lib/libgfarm/gfarm/gfs_pio_getline \
	lib/libgfarm/gfarm/gfs_pio_gets \
	lib/libgfarm/gfarm/gfs_pio_readline \
	lib/libgfarm/gfarm/gfs_pio_readdelim \
	lib/libgfarm/gfarm/gfs_pio_read \
	lib/libgfarm/gfarm/gfs_pio_putc \
	lib/libgfarm/gfarm/gfs_pio_putline \
	lib/libgfarm/gfarm/gfs_pio_puts \
	lib/libgfarm/gfarm/gfs_pio_write \
	lib/libgfarm/gfarm/gfs_stat \
	lib/libgfarm/gfarm/gfs_pio_truncate \
	lib/libgfarm/gfarm/gfs_chdir \
	lib/libgfarm/gfarm/gfs_realpath \
	lib/libgfarm/gfarm/gfs_opendir \
	lib/libgfarm/gfarm/gfs_closedir \
	lib/libgfarm/gfarm/gfs_readdir \
	lib/libgfarm/gfarm/gfs_glob_init \
	lib/libgfarm/gfarm/gfs_glob \
	lib/libgfarm/gfarm/gfs_glob_add \
	lib/libgfarm/gfarm/gfs_chmod \
	lib/libgfarm/gfarm/gfs_utimes \
	lib/libgfarm/gfarm/gfs_mkdir \
	lib/libgfarm/gfarm/gfs_rmdir \
	lib/libgfarm/gfarm/gfs_unlink \
	lib/libgfarm/gfarm/gfs_unlink_section \
	lib/libgfarm/gfarm/gfs_rename \
	lib/libgfarm/gfarm/gfarm_stringlist_init \
	lib/libgfarm/gfarm/gfarm_stringlist_add \
	lib/libgfarm/gfarm/gfarm_stringlist_elem \
	lib/libgfarm/gfarm/gfarm_stringlist_length \
	lib/libgfarm/gfarm/gfarm_stringlist_cat \
	lib/libgfarm/gfarm/GGFARM_STRINGLIST_ELEM \
	lib/libgfarm/gfarm/GGFARM_STRINGLIST_STRARRAY \
	lib/libgfarm/gfarm/gfs_pio_flush \
	lib/libgfarm/gfarm/gfs_pio_sync \
	lib/libgfarm/gfarm/gfs_pio_datasync \
	lib/libgfarm/gfarm/fork-term-init \
	lib/libgfarm/gfarm/seek-write \
	gfs_hook/lib/fsystest

check test: all
	@$(REGRESS_SH) -i
	@$(CHECK_ENV) $(TEST_TARGET)
	@echo ""
	@$(REGRESS_SH) -s

loop: all
	@$(CHECK_ENV) $(srcdir)/loop.sh

selfcheck: selfcheck_clear selfcheck_all_backends selfcheck_summary

selfcheck_clear:
	@$(REGRESS_SH) -i

selfcheck_all_backends:
	@echo ""
	@if [ "`echo $(backend_targets) | wc -w`" -eq 0 ]; \
	 then \
		echo "no usable metadata backend is configured"; \
		exit 1; \
	 fi;
	@for backend in $(backend_targets); do \
		$(MAKE) -f $(srcdir)/Makefile selfcheck-$${backend} || \
			exit $$?; \
	 done

selfcheck_summary:
	@echo ""
	@$(REGRESS_SH) -s

# this target is used by daily test on sfv100
selfcheck-postgresql2-and-ldap:
	@$(MAKE) -f $(srcdir)/Makefile selfcheck-postgresql2
	@$(MAKE) -f $(srcdir)/Makefile selfcheck-ldap

selfcheck-postgresql:
	@$(MAKE) -f $(srcdir)/Makefile selfcheck-postgresql1
	@$(MAKE) -f $(srcdir)/Makefile selfcheck-postgresql2

selfcheck-postgresql1 \
selfcheck-postgresql2 \
selfcheck-ldap:
	@backend=`expr $@ : 'selfcheck-\(.*\)'`; \
	 for auth in $(auth_targets); do \
		$(MAKE) -f $(srcdir)/Makefile $@-$${auth} || exit $$?; \
	 done

selfcheck-postgresql1-sharedsecret \
selfcheck-postgresql1-gsi \
selfcheck-postgresql1-gsi_auth \
selfcheck-postgresql2-sharedsecret \
selfcheck-postgresql2-gsi \
selfcheck-postgresql2-gsi_auth \
selfcheck-ldap-sharedsecret \
selfcheck-ldap-gsi \
selfcheck-ldap-gsi_auth:
	@backend=`expr $@ : 'selfcheck-\(.*\)-.*'`; \
	    auth=`expr $@ : 'selfcheck-.*-\(.*\)'`; \
	 for agent_mode in $(agent_targets); do \
		$(MAKE) -f $(srcdir)/Makefile $@-agent_$${agent_mode} \
			|| exit $$?; \
	 done

selfcheck-postgresql1-sharedsecret-agent_disable \
selfcheck-postgresql1-sharedsecret-agent_enable \
selfcheck-postgresql1-sharedsecret-agent_only \
selfcheck-postgresql1-gsi-agent_disable \
selfcheck-postgresql1-gsi-agent_enable \
selfcheck-postgresql1-gsi-agent_only \
selfcheck-postgresql1-gsi_auth-agent_disable \
selfcheck-postgresql1-gsi_auth-agent_enable \
selfcheck-postgresql1-gsi_auth-agent_only \
selfcheck-postgresql2-sharedsecret-agent_disable \
selfcheck-postgresql2-sharedsecret-agent_enable \
selfcheck-postgresql2-sharedsecret-agent_only \
selfcheck-postgresql2-gsi-agent_disable \
selfcheck-postgresql2-gsi-agent_enable \
selfcheck-postgresql2-gsi-agent_only \
selfcheck-postgresql2-gsi_auth-agent_disable \
selfcheck-postgresql2-gsi_auth-agent_enable \
selfcheck-postgresql2-gsi_auth-agent_only \
selfcheck-ldap-sharedsecret-agent_disable \
selfcheck-ldap-sharedsecret-agent_enable \
selfcheck-ldap-sharedsecret-agent_only \
selfcheck-ldap-gsi-agent_disable \
selfcheck-ldap-gsi-agent_enable \
selfcheck-ldap-gsi-agent_only \
selfcheck-ldap-gsi_auth-agent_disable \
selfcheck-ldap-gsi_auth-agent_enable \
selfcheck-ldap-gsi_auth-agent_only : all
	@backend=`expr $@ : 'selfcheck-\(.*\)-.*-agent_.*'`; \
	    auth=`expr $@ : 'selfcheck-.*-\(.*\)-agent_.*'`; \
	   agent=`expr $@ : 'selfcheck-.*-.*-agent_\(.*\)'`; \
	 opt=; \
	 case $${backend} in \
	 postgresql1) backend=postgresql; opt="-S 1";; \
	 postgresql2) backend=postgresql;; \
	 esac;\
	 echo "Trying -b $${backend} -a $${auth} --agent-$${agent} $${opt}" >&2; \
	 echo "Trying -b $${backend} -a $${auth} --agent-$${agent} $${opt}" >>$(LOG); \
	 $(SELFCHECK_ENV) $(srcdir)/config.sh --prefix $(prefix) \
		-b $${backend} -a $${auth} --agent-$${agent} $${opt} \
		--config-prefix $(CONFIG_PREFIX) $(CONFIG_OPT) \
			$(TEST_TARGET)

xfail-check:
	@cd $(srcdir) && \
	if find lib gfs_hook -type f -print | fgrep -v .svn | \
		egrep -v '/.#|/#|~$$' | \
		awk 'FILENAME=="XFAIL" && /[a-z]/ { documented[$$0]=1 } FILENAME=="-" { if (!documented[$$0]) print $$0 }' XFAIL - | \
		xargs fgrep -l exit_xfail | tee /dev/tty | grep '^'; \
	then \
		echo "Those are expected failures which are not documented"; \
		exit 1; \
	else \
		exit 0; \
	fi

all: config

config:
	test -f Makefile || \
	( \
	echo "regress=$(srcdir)"; \
	echo ". $(srcdir)/regress.conf"; \
	) > regress.conf

distclean:
	-$(RM) -f $(LOG)
	-test -f Makefile || $(RM) -f regress.conf

include $(top_srcdir)/makes/subdir.mk
