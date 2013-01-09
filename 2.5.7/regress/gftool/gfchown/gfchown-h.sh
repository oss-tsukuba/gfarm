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

if gfmkdir $gftmp &&
   gfreg $data/0byte $gftmp/file &&
   gfmkdir $gftmp/dir &&
   gfln -s file $gftmp/flink &&
   gfln -s dir $gftmp/dlink &&
   gfln -s not-exist $gftmp/dangling &&
   gfchown -h $user $gftmp/flink &&
   [ x"`gfls -ld $gftmp/flink    | awk '{ print $3 }'`" = x"$user" ] &&
   gfchown -h $user $gftmp/dlink &&
   [ x"`gfls -ld $gftmp/dlink    | awk '{ print $3 }'`" = x"$user" ]
   gfchown -h $user $gftmp/dangling &&
   [ x"`gfls -ld $gftmp/dangling | awk '{ print $3 }'`" = x"$user" ]
then
	exit_code=$exit_pass
fi

gfrm -rf $gftmp
exit $exit_code
