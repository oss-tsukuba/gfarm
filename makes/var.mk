include $(top_objdir)/makes/config.mk

COMMON_CFLAGS = $(OPTFLAGS) $(largefile_cflags) -I$(top_objdir) -I$(INCDIR)
COMMON_LDFLAGS = $(largefile_ldflags)

INCDIR = $(top_srcdir)/include
GFD_DIR = $(top_srcdir)/gfarmd
GFSD_DIR = $(top_srcdir)/gfsd
GFARMLIB_DIR = $(top_srcdir)/gfarmlib

NSLIB = -L$(top_objdir)/nslib -lns
DEPNSLIB = $(top_objdir)/nslib/libns.a

GFARMLIB = -L$(top_objdir)/gfarmlib -lgfarm \
	$(metadb_client_libs) $(openssl_libs)
DEPGFARMLIB = $(top_objdir)/gfarmlib/libgfarm.a
DEPGFARMINC = $(INCDIR)/gfarm.h $(INCDIR)/gfarm_error.h $(INCDIR)/gfarm_metadb.h $(INCDIR)/gfarm_misc.h $(INCDIR)/gfs.h
