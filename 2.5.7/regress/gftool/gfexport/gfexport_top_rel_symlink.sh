#!/bin/sh

. ./regress.conf

# relative path to symlink at top directory -> SIGSEGV. see:
# https://sourceforge.net/apps/trac/gfarm/ticket/393

# needs gfarmroot to write to top directory
if $regress/bin/am_I_gfarmroot; then
	:
else
	exit $exit_unsupported
fi

# do not use $gftmp, because this only happens at top directory
TMP=`hostname`."`echo $0 | sed s:/:_:g`".$$

trap 'gfrm -f ${TMP}; exit $exit_trap' $trap_sigs

if gfln -s ${TMP}XXX ${TMP}; then
	gfexport ${TMP} 2>/dev/null;
	case $? in
	1)	exit_code=$exit_pass;;
	esac
fi

gfrm -f ${TMP}
exit ${exit_code}

		