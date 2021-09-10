top_builddir = ../../../../../..
top_srcdir = $(top_builddir)
srcdir = .
toolsdir = ./tools/gen-data
testsdir = ./tools/testscripts
envname = test_dir

include $(top_srcdir)/makes/var.mk

PROGRAM = tls-test
SRCS = $(PROGRAM).c
OBJS = $(PROGRAM).lo
CFLAGS = $(COMMON_CFLAGS) -I$(GFARMLIB_SRCDIR) -I$(GFUTIL_SRCDIR) $(openssl_includes) -g3 -O0 -fkeep-inline-functions
LDLIBS = $(COMMON_LDLIBS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC)

###
env: $(toolsdir)
	@if test $${UID} -ne 0; then \
		echo "Need to be a super user. Use sudo."; \
		exit 1; \
	fi
	sudo -u $${SUDO_USER} $(toolsdir)/gen_certs_all.sh -o $(envname)/A && \
	sudo -u $${SUDO_USER} $(toolsdir)/gen_certs_all.sh \
		-o $(envname)/B -X 2 && \
	sudo -u $${SUDO_USER} $(toolsdir)/gen_certs_chain.sh -d $(envname) && \
	sudo -u $${SUDO_USER} $(toolsdir)/test_data_revocation.sh \
		-o $(envname) && \
	$(toolsdir)/test_data_permission_private_key.sh -o $(envname) && \
	$(toolsdir)/test_data_permission_crl_dir.sh -o $(envname) && \
	sudo -u $${SUDO_USER} $(toolsdir)/test_data_permission_cert.sh \
		-o $(envname) && \
	sudo -u $${SUDO_USER} $(toolsdir)/test_data_permission_cert_store.sh \
		-o $(envname)
	sudo -u $${SUDO_USER} $(toolsdir)/test_data_proxy_cert.sh -o $(envname)
	sudo -u $${SUDO_USER} $(toolsdir)/test_data_verify_chain_path.sh -o $(envname)
	@echo
	@echo
	@echo "NOTE: In case following tests require a passphrase, " \
	"use \"test\""
	@echo

run-test: $(testsdir) $(PROGRAM)
	$(testsdir)/run-test.sh

run-test-interactive: $(testsdir) $(PROGRAM)
	$(testsdir)/test3.sh

envclean:
	@if test $${UID} -ne 0; then \
		echo "Need to be a super user. Use sudo."; \
		exit 1; \
	fi
	@rm -rf $(envname) > /dev/null 2>&1


