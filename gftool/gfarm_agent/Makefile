# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

PROGRAM = gfarm_agent
SRCS = gfarm_agent.c agent_thr.c agent_ptable.c
OBJS = gfarm_agent.o agent_thr.o agent_ptable.o
CFLAGS = $(pthread_includes) \
	$(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	$(openssl_includes)
LDLIBS = $(pthread_libs) $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

gfarm_agent.o: $(DEPGFARMINC) \
	$(GFUTIL_SRCDIR)/gfutil.h \
	$(GFARMLIB_SRCDIR)/xxx_proto.h \
	$(GFARMLIB_SRCDIR)/io_fd.h \
	$(GFARMLIB_SRCDIR)/sockopt.h \
	$(GFARMLIB_SRCDIR)/metadb_access.h \
	$(GFARMLIB_SRCDIR)/agent_proto.h \
	$(GFARMLIB_SRCDIR)/agent_wrap.h \
	$(GFARMLIB_SRCDIR)/path_info_cache.h \
	agent_thr.h \
	agent_ptable.h
agent_thr.o: $(GFUTIL_SRCDIR)/gfutil.h \
	agent_thr.h
agent_ptable.o: agent_ptable.h
