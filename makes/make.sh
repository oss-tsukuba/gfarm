#!/bin/sh

# It isn't possible to simply invoke "make" on directories which don't
# have Makefile.in, if builddir is different from srcdir.
# This script can be used as an alternative of "make" on such situation.

: ${MAKE=make}

if [ -f Makefile ]; then
	exec ${MAKE} ${1+"$@"}
fi

# search ${top_builddir} - use sub shell to keep current directory
top_builddir=`sh -c '
top_builddir=.;
while	if pwd | grep ^/\$ >/dev/null; then
		echo '"$0"': cannot find \\${top_builddir} >&2;
		exit 1;
	fi;
	if [ -f Makefile -a -d makes -a -d gfptool ];
	then false; else :; fi;
do
	top_builddir=${top_builddir}/..;
	cd ..;
done;
echo ${top_builddir}
'`
case ${top_builddir} in
'')	exit 1;;
.)	:;;
*)	top_builddir="`echo ${top_builddir} | sed 's|^./||'`";;
esac

# search ${top_srcdir}
top_srcdir=`sed -n 's/^top_srcdir *= *//p' ${top_builddir}/Makefile`

# search ${srcdir}
top_builddir_abspath=`cd ${top_builddir} && pwd`
curdir=`pwd | sed "s|^${top_builddir_abspath}/||"`
srcdir=${top_srcdir}/${curdir}

case ${top_srcdir} in
/*)	:;;
*)	# ${top_srcdir} and ${srcdir} are relative paths?
	top_srcdir=${top_builddir}/${top_srcdir}
	srcdir=${top_builddir}/${srcdir}
	;;
esac

# echo top_builddir = ${top_builddir}
# echo top_srcdir = ${top_srcdir}
# echo srcdir = ${srcdir}

if [ -f ${srcdir}/Makefile.in ]; then
	exec ${MAKE} ${1+"$@"}
else
	exec ${MAKE} -f ${srcdir}/Makefile \
		top_srcdir=${top_srcdir} \
		srcdir=${srcdir} \
		${1+"$@"}
fi
