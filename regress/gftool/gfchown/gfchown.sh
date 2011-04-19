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
   gfchown $user $gftmp/file &&
   [ x"`gfls -ld $gftmp/file | awk '{ print $3 }'`" = x"$user" ] &&
   gfchown $user $gftmp/dir &&
   [ x"`gfls -ld $gftmp/dir  | awk '{ print $3 }'`" = x"$user" ] &&
   gfchown $me   $gftmp/file &&
   [ x"`gfls -ld $gftmp/file | awk '{ print $3 }'`" = x"$me" ] &&
   gfchown $me   $gftmp/dir &&
   [ x"`gfls -ld $gftmp/dir  | awk '{ print $3 }'`" = x"$me" ] &&
   gfchown $user $gftmp/flink &&
   [ x"`gfls -ld $gftmp/file | awk '{ print $3 }'`" = x"$user" ] &&
   gfchown $user $gftmp/dlink &&
   [ x"`gfls -ld $gftmp/dir  | awk '{ print $3 }'`" = x"$user" ]
then
	if gfchown $user $gftmp/dangling; then
		:
	else
		exit_code=$exit_pass
	fi
fi

gfrm -rf $gftmp
exit $exit_code
