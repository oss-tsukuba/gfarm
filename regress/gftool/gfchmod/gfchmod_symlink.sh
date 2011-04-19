#!/bin/sh

. ./regress.conf

trap 'gfrm -rf $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp &&
   gfreg $data/0byte $gftmp/file &&
   gfmkdir $gftmp/dir &&
   gfln -s file $gftmp/flink &&
   gfln -s dir $gftmp/dlink &&
   gfln -s not-exist $gftmp/dangling &&
   gfchmod 000 $gftmp/flink &&
   gfchmod 000 $gftmp/dlink &&
   [ x"`gfls -ld $gftmp/file | awk '{ print $1 }'`" = x"----------" ] &&
   [ x"`gfls -ld $gftmp/dir  | awk '{ print $1 }'`" = x"d---------" ] &&
   gfchmod 777 $gftmp/flink &&
   gfchmod 777 $gftmp/dlink &&
   [ x"`gfls -ld $gftmp/file | awk '{ print $1 }'`" = x"-rwxrwxrwx" ] &&
   [ x"`gfls -ld $gftmp/dir  | awk '{ print $1 }'`" = x"drwxrwxrwx" ]
then
	if gfchmod 777 $gftmp/dangling; then
		:
	else
		exit_code=$exit_pass
	fi
fi

gfrm -rf $gftmp
exit $exit_code
