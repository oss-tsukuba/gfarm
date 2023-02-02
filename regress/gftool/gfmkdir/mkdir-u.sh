#!/bin/sh

. ./regress.conf

if $regress/bin/am_I_gfarmroot; then
	:
else
	exit $exit_unsupported
fi
if [ `gfuser | sed 2q | wc -l` -ne 2 ]; then
	exit $exit_unsupported
fi

me=`gfwhoami`
user=`gfuser | sed 1q`
if [ x"$user" = x"$me" ]; then
	user=`gfuser | sed -n 2p`
fi

trap 'gfrm -rf $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir -u $user $gftmp &&
   [ x"`gfls -ld $gftmp | awk '{ print $3 }'`" = x"$user" ]
then
	exit_code=$exit_pass
fi

gfrm -rf $gftmp
exit $exit_code
