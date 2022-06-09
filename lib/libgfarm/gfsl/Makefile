# $Id$

top_builddir = ../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

###
### variables
###

CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFSL_SRCDIR) \
	$(GSSAPI_INCLUDES) \
	-DGFARM_INSTALL_ETC_DIR='"$(sysconfdir)"' \
	-DGLOBUS_FAKE_GSS_C_NT_USER=0 # see gss.c
LDLIBS = $(GSSAPI_LIBS) $(openssl_libs) $(LIBS)

### for library

# shared object version
LTLDFLAGS = $(LTFLAGS_SHARELIB_IN) $(libdir)
LT_CURRENT=1
LT_REVISION=0
LT_AGE=0

LIBRARY = libgfsl_$(GSSAPI_TYPE).la

SRCS = gss.c tcputil.c session.c misc.c
OBJS = gss.lo tcputil.lo session.lo misc.lo

### for programs

PURE = /usr/local/rational/releases/purify.sol.2001a.04.00/purify
# LINK = $(PURE) $(CC) $(CFLAGS)
# PROG_DEPLIBS = ../gfutil/.libs/libgfutil.a .libs/libgfsl.a

LINK = $(LTLINK)

PROGRAMS = server client server2 client2
SCOBJS = scarg.o scsub.o
SCOBJS2 = scarg.o
PROG_DEPLIBS = $(LIBRARY) ../gfutil/libgfutil.la
PROG_LDLIBS = $(COMMON_LDFLAGS) $(PROG_DEPLIBS) \
	$(GSSAPI_LIBS) $(openssl_libs) $(LIBS)

###
### rules
###

all: $(LIBRARY)

program: $(PROGRAMS)

test:
	echo "VPATH=<$(VPATH)>"

### for library

$(OBJS): $(DEPGFSLINC) $(DEPGFARMINC)

include $(top_srcdir)/makes/lib.mk
include $(top_srcdir)/makes/gflog.mk

# dependencies
gss.lo: $(GFUTIL_SRCDIR)/gfevent.h $(GFUTIL_SRCDIR)/gfutil.h
gss.lo: gfsl_config.h tcputil.h
misc.lo: $(GFUTIL_SRCDIR)/gfutil.h $(GFUTIL_SRCDIR)/thrsubr.h gfsl_config.h $(top_builddir)/makes/config.mk # $(GFARM_INSTALL_ETC_DIR) -> $(sysconfdir)
session.lo: $(GFUTIL_SRCDIR)/gfevent.h $(GFUTIL_SRCDIR)/gfutil.h
session.lo: tcputil.h misc.h
tcputil.lo: $(GFUTIL_SRCDIR)/gfnetdb.h $(GFUTIL_SRCDIR)/gfutil.h tcputil.h

server.o server2.o client.o client2.o: $(GFUTIL_SRCDIR)/gfutil.h \
    $(INC_SRCDIR)/gfarm_misc.h tcputil.h \
    gfsl_config.h gfsl_gss.h gfsl_secure_session.h misc.h scarg.h
server.o server2.o:
scarg.o: $(INC_SRCDIR)/gfarm_misc.h gfsl_gss.h misc.h scarg.h
scsub.o: gfsl_secure_session.h scarg.h \
    $(INC_SRCDIR)/gfarm_misc.h tcputil.h

### for programs

server:	server.o $(SCOBJS) $(PROG_DEPLIBS)
	rm -f $@
	$(LINK) -o $@ server.o $(SCOBJS) $(PROG_LDLIBS)

client:	client.o $(SCOBJS) $(PROG_DEPLIBS)
	rm -f $@
	$(LINK) -o $@ client.o $(SCOBJS) $(PROG_LDLIBS)

server2: server2.o $(SCOBJS2) $(PROG_DEPLIBS)
	rm -f $@
	$(LINK) -o $@ server2.o $(SCOBJS2) $(PROG_LDLIBS)

client2: client2.o $(SCOBJS2) $(PROG_DEPLIBS)
	rm -f $@
	$(LINK) -o $@ client2.o $(SCOBJS2) $(PROG_LDLIBS)

clean:
	$(RM) -f *.o

veryclean:
	$(LTCLEAN) $(PROGRAMS) *~ core *.core
