# $Id$

top_objdir = ../../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

# PURE = /usr/local/rational/releases/purify.sol.2001a.04.00/purify

PROGRAMS = server client server2 client2
SCOBJS = scsub.o

CFLAGS = $(COMMON_CFLAGS) -I$(GFSL_SRCDIR) $(globus_includes) \
	-DGLOBUS_BUG=1 # see gsi.c
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)

all:	$(PROGRAMS) 

server:	server.o $(SCOBJS) $(DEPGFARMLIB)
	rm -f $@
	$(PURE) $(CC) $(CFLAGS) -o $@ server.o $(SCOBJS) $(LDLIBS)

client:	client.o $(SCOBJS) $(DEPGFARMLIB)
	rm -f $@
	$(PURE) $(CC) $(CFLAGS) -o $@ client.o $(SCOBJS) $(LDLIBS)

server2: server2.o $(DEPGFARMLIB)
	rm -f $@
	$(PURE) $(CC) $(CFLAGS) -o $@ server2.o $(LDLIBS)

client2: client2.o $(DEPGFARMLIB)
	rm -f $@
	$(PURE) $(CC) $(CFLAGS) -o $@ client2.o $(LDLIBS)

clean:
	$(RM) -f $(PROGRAMS) *.o *~ core *.core

distclean: clean

gfregister:
man:
html:
