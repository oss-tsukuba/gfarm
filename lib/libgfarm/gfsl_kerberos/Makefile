top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

MKFLAGS= \
	GSSAPI_TYPE=kerberos \
	GSSAPI_INCLUDES='$(kerberos_includes)' \
	GSSAPI_LIBS='$(kerberos_gssapi_libs)'

all:

all install clean veryclean distclean man html msgno test:
	$(MAKE) -f $(srcdir)/../gfsl/Makefile \
		VPATH=$(srcdir)/../gfsl srcdir=$(srcdir)/../gfsl $(MKFLAGS) $@
