VPATH=$(srcdir)

include $(top_builddir)/makes/config.mk

ja_mandir = $(default_mandir)/ja

# example_bindir = $(default_bindir)/example
example_bindir = $(default_bindir)

metadb_client_includes = $(ldap_includes) $(postgresql_includes)
metadb_client_libs = $(ldap_libs) $(postgresql_libs)

RM = rm
GENCAT = gencat

# gflog
ASSIGNMSGNO=$(top_srcdir)/makes/assign_msgno.pl

# library to be installed, see lib.mk
LIBRARY_RESULT = $(LIBRARY)

# the following symbol is removed in gfarm-3.0 and later
#	-DCOMPAT_GFARM_2_3 - enable protocols which were deprecated in 2.4.0
COMMON_CFLAGS = $(OPTFLAGS) $(largefile_cflags) \
	-I$(top_builddir)/include -I$(top_srcdir)/include
COMMON_LDFLAGS = $(largefile_ldflags) $(dynamic_ldflags)
GFARMLIB = -L$(top_builddir)/lib/libgfarm -lgfarm \
	$(globus_gssapi_libs) $(openssl_libs)

INC_SRCDIR = $(top_srcdir)/include/gfarm
INC_BUILDDIR = $(top_builddir)/include/gfarm
DEPGFARMLIB = $(top_builddir)/lib/libgfarm/libgfarm.la
DEPGFARMINC = $(INC_BUILDDIR)/gfarm_config.h $(INC_SRCDIR)/gfarm.h $(INC_SRCDIR)/gflog.h $(INC_SRCDIR)/error.h $(INC_SRCDIR)/gfarm_misc.h $(INC_SRCDIR)/gfarm_stringlist.h $(INC_SRCDIR)/gfs.h $(INC_SRCDIR)/gfs_glob.h

# ns

NS_COMMON_CFLAGS = $(OPTFLAGS) $(largefile_cflags) \
	-I$(top_builddir)/include \
	-I$(top_builddir)/ns/include -I$(top_srcdir)/ns/include
NSLIB = -L$(top_builddir)/ns/nslib -lns
NSEXECLIB = -L$(top_builddir)/ns/nslib -lnsexec

NSINC_SRCDIR = $(top_srcdir)/ns/include/gfarm
NSINC_BUILDDIR = $(top_builddir)/ns/include/gfarm
GFD_SRCDIR = $(top_srcdir)/ns/gfarmd
DEPNSLIB = $(top_builddir)/ns/nslib/libns.la
DEPNSEXECLIB = $(top_builddir)/ns/nslib/libnsexec.la

# gfsl

GFSL_SRCDIR = $(top_srcdir)/lib/libgfarm/gfsl
DEPGFSLINC = $(GFSL_SRCDIR)/gfarm_auth.h $(GFSL_SRCDIR)/gfarm_gsi.h $(GFSL_SRCDIR)/gfarm_secure_session.h

# libgfarm internal interface

GFUTIL_SRCDIR = $(top_srcdir)/lib/libgfarm/gfutil
GFARMLIB_SRCDIR = $(top_srcdir)/lib/libgfarm/gfarm

# gfmd

GFMD_SRCDIR = $(top_srcdir)/server/gfmd
GFMD_BUILDDIR = $(top_builddir)/server/gfmd

# doc & man

XSLTPROC = xsltproc
DOCBOOK_XSLDIRS= \
	/usr/share/xml/docbook/stylesheet/docbook-xsl \
	/usr/share/xml/docbook/stylesheet/docbook-xsl-ns \
	/usr/share/sgml/docbook/xsl-stylesheets \
	/usr/local/share/xsl/docbook \
	/usr/local/share/xsl/docbook-ns \
	/usr/pkg/share/xsl/docbook

srcsubst = dummy
dstsubst = dummy

# libtool

LTFLAGS_SHARELIB_IN = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) -rpath
CCLD = $(CC)

LTCOMPILE = $(LIBTOOL) --mode=compile $(CC) $(CFLAGS)
LTLINK = $(LIBTOOL) --mode=link $(CCLD) $(CFLAGS) $(LTLDFLAGS) $(LDFLAGS) -o $@
LTCLEAN = $(LIBTOOL) --mode=clean $(RM) -f
# the use of the following operations should honor $(DESTDIR)
LTINSTALL_PROGRAM = $(LIBTOOL) --mode=install $(INSTALL_PROGRAM)
LTINSTALL_LIBRARY = $(LIBTOOL) --mode=install $(INSTALL_DATA)

# private src

private_dir = ./private

.SUFFIXES: .a .la .ln .o .lo .s .S .c .cc .f .y .l .msg .cat

.c.lo:
	$(LTCOMPILE) -c $(srcdir)/$*.c

.s.lo:
	$(LTCOMPILE) -c $(srcdir)/$*.s

.S.lo:
	$(LTCOMPILE) -c $(srcdir)/$*.S

.msg.cat:
	cat $(srcdir)/$*.msg | $(CPP) $(CFLAGS) - 2>/dev/null | \
		grep '^[0-9$$]' > tmp$$$$.msg; \
	LC_ALL=$(LOCALE) $(GENCAT) $@ tmp$$$$.msg; rv=$$?; \
	rm tmp$$$$.msg; \
	exit $${rv}


