# $Id$

AC_INIT

TOPDIR=${ac_pwd}
AC_SUBST(TOPDIR)

AC_CONFIG_AUX_DIR(./mk)
AC_CONFIG_MACRO_DIR(./mk)

if test "${prefix}" = "NONE"; then
    prefix=/usr/local
fi
if test "${exec_prefix}" = "NONE"; then
    exec_prefix='${prefix}'
fi

SHELL=`which bash` > /dev/null 2>&1
if test $? -ne 0 -o -z "${SHELL}"; then
   AC_MSG_ERROR([To run libtool you need a bash, not /bin/sh.])
   exit 1
fi
export SHELL
AC_SUBST(SHELL)

LT_INIT
AC_SUBST(LIBTOOL)
AC_SUBST(LIBTOOL_DEPS)

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AC_PROG_INSTALL
AC_PROG_CC
AC_PROG_CXX

RM="rm -f"
AC_SUBST(RM)

MKDIR="mkdir -p"
AC_SUBST(MKDIR)


# headers/macros

AC_CONFIG_HEADER([src/include/nata/nata_config.h])

AC_C_BIGENDIAN

AC_HEADER_STDC

AC_CHECK_HEADER(inttypes.h, [AC_DEFINE(HAVE_INTTYPES_H)], [])
AC_CHECK_HEADER(stdint.h, [AC_DEFINE(HAVE_STDINT_H)])
AC_CHECK_HEADER(limits.h, [AC_DEFINE(HAVE_LIMITS_H)])
AC_CHECK_HEADER(sys/param.h, [AC_DEFINE(HAVE_SYS_PARAM_H)])
AC_CHECK_HEADER(sys/types.h, [AC_DEFINE(HAVE_SYS_TYPES_H)])
AC_CHECK_HEADER(ctype.h, [AC_DEFINE(HAVE_CTYPE_H)])
AC_CHECK_HEADER(stdio.h, [AC_DEFINE(HAVE_STDIO_H)])
AC_CHECK_HEADER(stdlib.h, [AC_DEFINE(HAVE_STDLIB_H)])
AC_CHECK_HEADER(unistd.h, [AC_DEFINE(HAVE_UNISTD_H)])
AC_CHECK_HEADER(memory.h, [AC_DEFINE(HAVE_MEMORY_H)])
AC_CHECK_HEADER(string.h, [AC_DEFINE(HAVE_STRING_H)])
AC_CHECK_HEADER(malloc.h, [AC_DEFINE(HAVE_MALLOC_H)])
AC_CHECK_HEADER(stdarg.h, [AC_DEFINE(HAVE_STDARG_H)])
AC_CHECK_HEADER(dirent.h, [AC_DEFINE(HAVE_DIRENT_H)])
AC_CHECK_HEADER(time.h, [AC_DEFINE(HAVE_TIME_H)])
AC_CHECK_HEADER(sys/time.h, [AC_DEFINE(HAVE_SYS_TIME_H)])
AC_CHECK_HEADER(sys/termios.h, [AC_DEFINE(HAVE_SYS_TERMIOS_H)])
AC_CHECK_HEADER(sys/stat.h, [AC_DEFINE(HAVE_SYS_STAT_H)])
AC_CHECK_HEADER(fcntl.h, [AC_DEFINE(HAVE_FCNTL_H)])
AC_CHECK_HEADER(errno.h, [AC_DEFINE(HAVE_ERRNO_H)])
AC_CHECK_HEADER(signal.h, [AC_DEFINE(HAVE_SIGNAL_H)])
AC_CHECK_HEADER(netdb.h, [AC_DEFINE(HAVE_NETDB_H)])
AC_CHECK_HEADER(netinet/tcp.h, [AC_DEFINE(HAVE_NETINET_TCP_H)])
AC_CHECK_HEADER(sys/socket.h, [AC_DEFINE(HAVE_SYS_SOCKET_H)])
AC_CHECK_HEADER(arpa/inet.h, [AC_DEFINE(HAVE_ARPA_INET_H)])
AC_CHECK_HEADER(sys/ioctl.h, [AC_DEFINE(HAVE_SYS_IOCTL_H)])
AC_CHECK_HEADER(sys/wait.h, [AC_DEFINE(HAVE_SYS_WAIT_H)])
AC_CHECK_HEADER(pthread.h, [AC_DEFINE(HAVE_PTHREAD_H)])
AC_CHECK_HEADER(stdbool.h, [AC_DEFINE(HAVE_STDBOOL_H)])

AC_CHECK_FUNC(strtold, [AC_DEFINE(HAVE_STRTOLD)])

# Assume 32 bit for cross compilation.
AC_CHECK_SIZEOF(short int, 2)
AC_CHECK_SIZEOF(int, 4)
AC_CHECK_SIZEOF(long int, 4)
AC_CHECK_SIZEOF(long long, 8)
AC_CHECK_SIZEOF(int64_t, 8)
AC_CHECK_SIZEOF(size_t, 4)
AC_CHECK_SIZEOF(void *, 4)

oCFLAGS=${CFLAGS}
CFLAGS="${CFLAGS} -Wall -Werror"
AC_MSG_CHECKING([print format for size_t])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM(
		[[#include <stdio.h>]
		 [#include <stddef.h>]
		 [#include <string.h>]],
		[[char buf[24];]
		 [snprintf(buf, sizeof(buf), "%zu", (size_t)1);]
		 [return (strcmp("1", buf) == 0 && strlen(buf) == 1) ? ]
		 [0 : 1;]])],
	[AC_DEFINE(HAVE_PRINT_FORMAT_FOR_SIZE_T)
	 AC_MSG_RESULT([available.])],
	[AC_MSG_RESULT([not available.])],
	[AC_MSG_RESULT([not checkable since cross-compilation.])])
CFLAGS=${oCFLAGS}

# sysdeps.

eval `echo ${target} | awk -F- '{ printf "tagCPU=%s; tagOS=%s;\n", $1, $3 }'`

changequote(<<, >>)dnl
CPU=`echo ${tagCPU} | tr '[A-Z]' '[a-z]'`
OS=`echo ${tagOS} | tr '[A-Z]' '[a-z]'` 
changequote([, ])dnl
unset tagOS tagCPU

case $CPU in
    i*86)
        CPU=i386;;
esac

OS_CPPFLAGS='-D_REENTRANT'
OS_LDFLAGS=''
OS_LIBS=''
OS_CFLAGS=''
if test -z "${MAKE}"; then
	MAKE=make
fi

API_FLAVOR=NATA_API_POSIX
TARGET_ARCH=POSIX
case $OS in
    sunos*)
        OS=sunos;;
    solaris*)
        OS=solaris;;
    freebsd*)
	OS=freebsd;;
    linux*)
	OS_CPPFLAGS="${OS_CPPFLAGS} -D_GNU_SOURCE"
	OS_LIBS="-lrt -lpthread"
	OS=linux;;
    netbsd*)
        MAKE=gmake
        OS_CPPFLAGS="${OS_CPPFLAGS} -I/usr/pkg/include"
    	OS_LDFLAGS="-L/usr/pkg/lib"
	OS_LIBS="-lpthread"
	OS=netbsd;;
    cygwin*)
	OS=cygwin;;
    mingw32ce)
	OS=windowsce
	TARGET_ARCH=WINDOWS
	API_FLAVOR=NATA_API_WIN32API;;
    mingw32*)
	case $CPU in
	    x86_64*)
		OS=windows64;;
	    i*86)
		OS=windows32;;
	    *)
		OS=windows;;
	esac
	OS_LIBS="-lpthread -loleaut32 -lws2_32 -lwinmm"
	TARGET_ARCH=WINDOWS
	API_FLAVOR=NATA_API_WIN32API;;
esac

OS_CXXFLAGS=${OS_CFLAGS}
AC_SUBST(MAKE)
AC_SUBST(OS_CPPFLAGS)
AC_SUBST(OS_CFLAGS)
AC_SUBST(OS_CXXFLAGS)
AC_SUBST(OS_LDFLAGS)
AC_SUBST(OS_LIBS)

changequote(<<, >>)dnl
CPUDEF=NATA_CPU_`echo $CPU | tr '[a-z]' '[A-Z]'`
OSDEF=NATA_OS_`echo $OS | tr '[a-z]' '[A-Z]'`
changequote([, ])dnl
AC_SUBST(CPUDEF)
AC_SUBST(OSDEF)
AC_SUBST(API_FLAVOR)
AC_SUBST(TARGET_ARCH)

MKRULESDIR=${TOPDIR}/mk
AC_SUBST(MKRULESDIR)

AC_SUBST(CPPFLAGS)
AC_SUBST(CFLAGS)
AC_SUBST(CXXFLAGS)
AC_SUBST(LDFLAGS)

# emit.
AC_CONFIG_FILES(
	mk/vars.mk
	Makefile
	src/lib/Makefile
	src/include/nata/Makefile
	src/include/nata/nata_platform.h
	src/cmd/Makefile
)

AC_CONFIG_COMMANDS([modify-config],
	[sh mk/finalizeconfig ${OS_CPPFLAGS}],
	[OS_CPPFLAGS="${OS_CPPFLAGS}"])

AC_CONFIG_COMMANDS([includes],
	[sh mk/initheaders])

AC_CONFIG_COMMANDS([depend],
	[sh mk/initdepend && ${MAKE} depend],
	[MAKE=${MAKE}])

AC_CONFIG_COMMANDS([config-clean],
	[rm -rf ./config.log ./config.status.* autom4te.cache])

AC_OUTPUT

${MAKE} clean

echo ""
echo "CC:	'${CC}'"
echo "CFLAGS:	'${CFLAGS} ${OS_CFLAGS}'"
echo "CXX:	'${CXX}'"
echo "CXXFLAGS:	'${CXXFLAGS} ${OS_CFLAGS}'"
echo "CPPFLAGS:	'${CPPFLAGS} ${OS_CPPFLAGS} ${CPUDEF} ${OSDEF} ${API_FLAVOR}'"
echo "LDFLAGS:	'${LDFLAGS} ${OS_LDFLAGS} ${OS_LIBS}'"
echo ""
