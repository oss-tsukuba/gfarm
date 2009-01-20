AC_INIT([thput-fsys], [0.9.3])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AM_MAINTAINER_MODE
AC_PROG_CC
AC_SYS_LARGEFILE
ACX_MPI([mpi=true], [mpi=false])
AM_CONDITIONAL([MPI], [test x$mpi = xtrue])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile mpi/Makefile])
AC_OUTPUT
