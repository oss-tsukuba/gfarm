top_builddir = ../../../../../..
top_srcdir = $(top_builddir)
srcdir = .
toolsdir = ./tools/gen-data
envname = gfarm_environment

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
	$(toolsdir)/gen_certs_all.sh -o $(envname)/A
	$(toolsdir)/gen_certs_all.sh -o $(envname)/B -X 2
	$(toolsdir)/gen_certs_chain.sh -d $(envname)
	$(toolsdir)/test_data_revocation.sh -o $(envname)
	sudo $(toolsdir)/test_data_permission_private_key.sh -o $(envname)
	sudo $(toolsdir)/test_data_permission_crl_dir.sh -o $(envname)
	$(toolsdir)/test_data_permission_cert.sh -o $(envname)
	$(toolsdir)/test_data_permission_cert_store.sh -o $(envname)

envclean: 
	sudo rm -rfv $(envname)
