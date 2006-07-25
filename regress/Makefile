top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

POSTGRESQL_TARGETS=postgresql
LDAP_TARGETS=ldap
backend_candidates=${postgresql_targets} ${ldap_targets}

check test:
	@echo ""
	@${srcdir}/regress.sh
	@echo ""

selfcheck:
	@echo ""
	@if [ "`echo ${backend_candidates} | wc -w`" -eq 0 ]; \
	 then \
		echo "no usable metadata backend is configured"; \
		exit 1; \
	 fi;
	@for backend in ${backend_candidates}; do \
		echo "Trying -b $$backend"; \
		${srcdir}/config.sh --prefix ${prefix} \
		-h boaz \
			-b $$backend --config-prefix /var/tmp/`whoami` \
			${srcdir}/regress.sh; \
	 done
	@echo ""

loop:
	@./loop.sh
