top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

POSTGRESQL_TARGETS=postgresql
LDAP_TARGETS=ldap

check test:
	@echo ""
	@./regress.sh
	@echo ""

selfcheck:
	@echo ""
	@default_backend=`echo ${postgresql_targets} ${ldap_targets} | \
		awk '{print $$1}'`; \
		if [ -z "$$default_backend" ]; then \
			echo "no usable metadata backend is configured"; \
			exit 1; \
		fi; \
		prefix=${prefix} ./config.sh -b $$default_backend \
			/var/tmp/`whoami` ./regress.sh
	@echo ""

loop:
	@./loop.sh
