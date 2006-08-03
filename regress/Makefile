top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

POSTGRESQL_TARGETS=postgresql
LDAP_TARGETS=ldap
backend_candidates=${postgresql_targets} ${ldap_targets}

CONFIG_PREFIX=	/var/tmp/`whoami`
CONFIG_OPT=	# -k
TEST_TARGET=	${srcdir}/regress.sh

# These environment settings aren't enough,
# and .bashrc, .zshenv or .cshrc must be modified to set those environments,
# otherwise gfrcmd and gfrun may not work correctly.
# But these settings save tests other than those.
CHECK_ENV=	LANG=C PATH=$(prefix)/bin:$$PATH
SELFCHECK_ENV=	LANG=C

check test:
	@echo ""
	@$(CHECK_ENV) $(TEST_TARGET)
	@echo ""

loop:
	@$(CHECK_ENV) ./loop.sh

selfcheck:
	@echo ""
	@if [ "`echo ${backend_candidates} | wc -w`" -eq 0 ]; \
	 then \
		echo "no usable metadata backend is configured"; \
		exit 1; \
	 fi;
	@for backend in ${backend_candidates}; do \
		$(MAKE) -f $(srcdir)/Makefile selfcheck-$${backend} || \
			exit $$?; \
	 done
	@echo ""

selfcheck-postgresql selfcheck-ldap:
	@backend=`expr $@ : 'selfcheck-\(.*\)'`; \
	echo "Trying -b $$backend"; \
	$(SELFCHECK_ENV) ${srcdir}/config.sh --prefix ${prefix} -b $$backend \
		--config-prefix $(CONFIG_PREFIX) $(CONFIG_OPT) \
		${srcdir}/gfs_hook.sh --prefix ${prefix} \
			$(TEST_TARGET)
