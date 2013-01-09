#!/bin/sh

. ./regress.conf

trap 'gfrm -rf $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp &&
   gfreg $data/0byte $gftmp/file &&
   gfmkdir $gftmp/dir &&
   gfln -s file $gftmp/flink &&
   gfln -s dir $gftmp/dlink &&
   gfln -s not-exist $gftmp/dangling &&
   gfchmod -h 000 $gftmp/flink &&
   gfchmod -h 000 $gftmp/dlink &&
   gfchmod -h 000 $gftmp/dangling &&
   [ x"`gfls -ld $gftmp/flink    | awk '{ print $1 }'`" = x"l---------" ] &&
   [ x"`gfls -ld $gftmp/dlink    | awk '{ print $1 }'`" = x"l---------" ] &&
   [ x"`gfls -ld $gftmp/dangling | awk '{ print $1 }'`" = x"l---------" ] &&
   gfchmod -h 777 $gftmp/flink &&
   gfchmod -h 777 $gftmp/dlink &&
   gfchmod -h 777 $gftmp/dangling &&
   [ x"`gfls -ld $gftmp/flink    | awk '{ print $1 }'`" = x"lrwxrwxrwx" ] &&
   [ x"`gfls -ld $gftmp/dlink    | awk '{ print $1 }'`" = x"lrwxrwxrwx" ]
   [ x"`gfls -ld $gftmp/dangling | awk '{ print $1 }'`" = x"lrwxrwxrwx" ]
then
	exit_code=$exit_pass
fi

gfrm -rf $gftmp
exit $exit_code
