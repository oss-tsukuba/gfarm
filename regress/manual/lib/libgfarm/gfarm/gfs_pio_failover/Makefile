top_builddir = ../../../../../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

PROGRAM = gfs_pio_failover_test
SRCS = $(PROGRAM).c
OBJS = $(PROGRAM).o
CFLAGS = $(COMMON_CFLAGS) -I$(GFARMLIB_SRCDIR) -I$(GFUTIL_SRCDIR)
LDLIBS = $(COMMON_LDLIBS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

check:
	env srcdir=$(srcdir) $(srcdir)/test-all.sh auto

# e.g.
#	$ make TEST_TYPE=realpath check-one
check-one:
	env srcdir=$(srcdir) $(srcdir)/test-launch.sh $(TEST_TYPE) auto

failover:
	env srcdir=$(srcdir) ./gfmd-failover-local.sh

###

$(OBJS): $(DEPGFARMINC)
