# $Id$

top_srcdir = ../..

include $(top_srcdir)/makes/var.mk

SRCS = gfkey.c
OBJS = gfkey.o
PROGRAM = gfkey
CFLAGS = $(openssl_includes) -I$(top_srcdir) -I$(INCDIR) \
	-I$(top_srcdir)/gfarmlib $(OPTFLAGS)
LDLIBS = -L$(top_srcdir)/gfarmlib -lgfarm \
	$(metadb_client_libs) $(openssl_libs) $(LIBS)
DEPLIBS = $(top_srcdir)/gfarmlib/libgfarm.a

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(INCDIR)/gfarm.h $(INCDIR)/gfarm_error.h $(INCDIR)/gfarm_metadb.h $(INCDIR)/gfarm_misc.h $(INCDIR)/gfs.h
