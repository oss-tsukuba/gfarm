include $(top_objdir)/makes/config.mk

ja_mandir = $(default_mandir)/ja

# example_bindir = $(default_bindir)/example
example_bindir = $(default_bindir)

RM = rm

COMMON_CFLAGS = $(OPTFLAGS) $(largefile_cflags) \
	-I$(top_objdir) -I$(top_objdir)/include
COMMON_LDFLAGS = $(largefile_ldflags)

INCDIR = $(top_srcdir)/include
OBJINCDIR = $(top_objdir)/gfarm
GFSL_DIR = $(top_srcdir)/lib/libgfsl
GFD_DIR = $(top_srcdir)/gfarmd
GFSD_DIR = $(top_srcdir)/gfsd
GFARMLIB_DIR = $(top_srcdir)/gfarmlib

GFSL_INCLUDES = -I$(GFSL_DIR)
GFSL_LIBS = -L$(top_objdir)/lib/libgfsl -lgfsl
DEPGFSLLIB = $(top_objdir)/lib/libgfsl/libgfsl.a
DEPGFSLINC = $(GFSL_DIR)/gfarm_auth.h $(GFSL_DIR)/gfarm_gsi.h $(GFSL_DIR)/gfarm_secure_session.h

NSLIB = -L$(top_objdir)/nslib -lns
DEPNSLIB = $(top_objdir)/nslib/libns.a

GFARMLIB = -L$(top_objdir)/gfarmlib -lgfarm \
	$(metadb_client_libs) $(gfsl_libs) $(openssl_libs)
DEPGFARMLIB = $(top_objdir)/gfarmlib/libgfarm.a $(depgfsllib)
DEPGFARMINC = $(INCDIR)/gfarm.h $(INCDIR)/gfarm_error.h $(INCDIR)/gfarm_metadb.h $(INCDIR)/gfarm_misc.h $(INCDIR)/gfarm_stringlist.h $(INCDIR)/gfs.h $(INCDIR)/gfs_glob.h

DOCBOOK2MAN = jw -b man
DOCBOOK2HTML = jw -b html -u
srcsubst = dummy
dstsubst = dummy

