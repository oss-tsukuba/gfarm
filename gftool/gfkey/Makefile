# $Id$

top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfkey.c
OBJS = gfkey.o
PROGRAM = gfkey
CFLAGS = $(openssl_includes) -I$(top_objdir) -I$(INCDIR) \
	-I$(top_srcdir)/gfarmlib $(OPTFLAGS)
LDLIBS = $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC)
