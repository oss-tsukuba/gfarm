# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

# shared object version
LT_CURRENT=0
LT_REVISION=0
LT_AGE=0

LTLINK_SHARELIB= $(LTLINK) $(LTFLAGS_SHARELIB_IN) $(libdir)

DEBUG_CFLAGS=
SUFFIX=

DEBUG_MAKEFLAGS= \
	DEBUG_CFLAGS=-DDEBUG \
	SUFFIX=_debug

SUBDIRS= sysdep/$(gfs_hook_sysdep_subdir)
SYSDEP_OBJS= sysdep/$(gfs_hook_sysdep_subdir)/libsysdep.la

# do not use $(COMMON_CFLAGS) here, to avoid symbol renaming by largefile API.
CFLAGS = $(OPTFLAGS) $(DEBUG_CFLAGS) -I$(srcdir) \
	-I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) $(openssl_includes) \
	-I$(top_builddir)/include -I$(top_srcdir)/include

# The reason, why we don't use $(GFARMLIB) and add an inter-library
# dependency only on libgfarm here, is because the other dependencies
# will be automatically added by libtool. And because using $(GFARMLIB)
# automatically adds $(globus_gssapi_libs), and that may add statically
# linked globus libraries to the libgfs_hook.a.
LDLIBS = $(top_builddir)/lib/libgfarm/libgfarm.la

# to not use default $(LIBRARY) target in lib.mk
LIBRARY =

LIBRARY_RESULT = libgfs_hook$(SUFFIX).la gfs_hook$(SUFFIX).o \
	libgfs_hook_no_init$(SUFFIX).la gfs_hook_no_init$(SUFFIX).o \
	$(mpi_targets)
MPI_TARGETS = libgfs_hook_mpi$(SUFFIX).la gfs_hook_mpi$(SUFFIX).o

# need to hook _LFS64_LARGEFILE API?
LARGEFILE64_SRCS= hooks_64.c lseek64.c
LARGEFILE64_OBJS= hooks_64$(SUFFIX).lo lseek64.lo

# build MPI related objects?
MPI_SRCS = hooks_init_mpi.c
MPI_OBJS = hooks_init_mpi$(SUFFIX).lo

SRCS = getcwd.c hooks_subr.c \
	hooks.c \
	$(largefile64_srcs) \
	hooks_common.c hooks_stat.c hooks_fstat.c \
	hooks_init.c $(mpi_srcs)
OBJS = getcwd.lo hooks_subr$(SUFFIX).lo \
	hooks$(SUFFIX).lo \
	$(largefile64_objs) \
	$(SYSDEP_OBJS)
EXTRA_CLEAN_TARGETS= \
	lib_tmp_gfs_hook$(SUFFIX).la \
	lib_tmp_gfs_hook_no_init$(SUFFIX).la \
	lib_tmp_gfs_hook_mpi$(SUFFIX).la \
	hooks_init$(SUFFIX).lo $(mpi_objs)

###
### settings for test programs
###

TEST_LDLIBS = $(GFARMLIB)

###
### main rule
###

.SUFFIXES: .a .la .ln .o _debug.lo .lo .s .S .c .cc .f .y .l

.c_debug.lo:
	$(LTCOMPILE) -c $(srcdir)/$*.c -o $*_debug.lo

all: subdir-all gfs-hooks debug-gfs-hooks

###
### rules to make $(LIBRARY_RESULT)
###

gfs-hooks: $(LIBRARY_RESULT)


lib_tmp_gfs_hook$(SUFFIX).la: $(OBJS) hooks_init$(SUFFIX).lo
	$(LTLINK) $(OBJS) hooks_init$(SUFFIX).lo

gfs_hook$(SUFFIX).o: lib_tmp_gfs_hook$(SUFFIX).la
	$(LTLINK) lib_tmp_gfs_hook$(SUFFIX).la

libgfs_hook$(SUFFIX).la: lib_tmp_gfs_hook$(SUFFIX).la
	$(LTLINK_SHARELIB) lib_tmp_gfs_hook$(SUFFIX).la $(LDLIBS)


lib_tmp_gfs_hook_no_init$(SUFFIX).la: $(OBJS)
	$(LTLINK) $(OBJS)

gfs_hook_no_init$(SUFFIX).o: lib_tmp_gfs_hook_no_init$(SUFFIX).la
	$(LTLINK) lib_tmp_gfs_hook_no_init$(SUFFIX).la

libgfs_hook_no_init$(SUFFIX).la: lib_tmp_gfs_hook_no_init$(SUFFIX).la
	$(LTLINK_SHARELIB) lib_tmp_gfs_hook_no_init$(SUFFIX).la $(LDLIBS)


lib_tmp_gfs_hook_mpi$(SUFFIX).la: $(OBJS) hooks_init_mpi$(SUFFIX).lo
	$(LTLINK) $(OBJS) hooks_init_mpi$(SUFFIX).lo

gfs_hook_mpi$(SUFFIX).o: lib_tmp_gfs_hook_mpi$(SUFFIX).la
	$(LTLINK) lib_tmp_gfs_hook_mpi$(SUFFIX).la

libgfs_hook_mpi$(SUFFIX).la: lib_tmp_gfs_hook_mpi$(SUFFIX).la
	$(LTLINK_SHARELIB) lib_tmp_gfs_hook_mpi$(SUFFIX).la $(LDLIBS)

###
### objects needed by $(LIBRARY_TARGET)
###

getcwd.lo: getcwd.c $(INC_SRCDIR)/gfarm_misc.h hooks_subr.h

hooks_subr$(SUFFIX).lo: hooks_subr.c
	$(LTCOMPILE) $(largefile_cflags) -o $@ -c $(srcdir)/hooks_subr.c

lseek64.lo: lseek64.c
	$(LTCOMPILE) -D_LARGEFILE64_SOURCE `getconf LFS64_CFLAGS` -o $@ -c $(srcdir)/lseek64.c

hooks_64$(SUFFIX).lo: hooks_64.c hooks_common.c hooks_stat.c hooks_fstat.c
	$(LTCOMPILE) -D_LARGEFILE64_SOURCE `getconf LFS64_CFLAGS` -o $@ -c $(srcdir)/hooks_64.c

hooks$(SUFFIX).lo: hooks.c hooks_common.c hooks_stat.c hooks_fstat.c

hooks_init_mpi$(SUFFIX).lo: hooks_init_mpi.c
	$(LIBTOOL) --mode=compile $(MPICC) $(CFLAGS) -o $@ -c $(srcdir)/hooks_init_mpi.c

install: all
	# XXX this installation directory is WRONG.
	@$(MKDIR_P) $(DESTDIR)$(libdir)
	$(INSTALL_DATA) $(srcdir)/hooks_init_mpi.c \
		$(DESTDIR)$(libdir)/hooks_init_mpi.c

###
### *_debug.{o,a,so*}: libraries compiled with -DDEBUG
###

install: debug-lib-install
clean: debug-lib-clean
veryclean: debug-lib-veryclean

debug-gfs-hooks debug-lib-install debug-lib-clean debug-lib-veryclean:
	@target=`expr $@ : 'debug-\(.*\)'`; \
	echo '[' making $@ ']'; \
	echo \
	$(MAKE) -f $(srcdir)/Makefile $(DEBUG_MAKEFLAGS) $${target}; \
	$(MAKE) -f $(srcdir)/Makefile $(DEBUG_MAKEFLAGS) $${target}

###
### test programs, these won't be installed.
###

veryclean: test-veryclean

test: hook_test
mpitest: hook_test_mpi

hook_test: subdir-all debug-gfs-hooks hook_test.o
	$(LTLINK) hook_test.o gfs_hook_debug.o $(TEST_LDLIBS)

hook_test_mpi: subdir-all debug-gfs-hooks hook_test_mpi.o
	$(LIBTOOL) --mode=link $(MPICC) $(CFLAGS) -o $@ hook_test_mpi.o gfs_hook_mpi_debug.o $(TEST_LDLIBS)

hook_test_mpi.o: hook_test_mpi.c
	$(MPICC) -c $(CFLAGS) -o $@ $(srcdir)/hook_test_mpi.c

test-veryclean:
	$(LTCLEAN) hook_test hook_test.o hook_test_mpi hook_test_mpi.o

###
### dependencies, etc.
###

include $(top_srcdir)/makes/subdir.mk
include $(top_srcdir)/makes/lib.mk

$(OBJS) hooks_64$(SUFFIX).lo: $(DEPGFARMINC) hooks_subr.h

hooks_subr$(SUFFIX).lo: \
	$(GFUTIL_SRCDIR)/hash.h \
	$(GFARMLIB_SRCDIR)/config.h \
	$(GFARMLIB_SRCDIR)/gfs_pio.h

# libtool sometimes fails to remove .libs, and leaves ".libs/lib*.so.*T"
post-distclean-hook:
	-rm -rf .libs
