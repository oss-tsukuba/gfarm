#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

#FLAGS=0x00000400	# GFARM_FILE_RDONLY|GFARM_FILE_TRUNC
FLAGS=0x00000402	# GFARM_FILE_RDWR|GFARM_FILE_TRUNC

if gfreg $data/1byte $gftmp &&
   $testbin/open_read $gftmp $FLAGS >$localtmp &&
   [ `LC_ALL=C wc -c <$localtmp` = 0 ] &&
   gfls -l $gftmp |
	awk '{size=$5; print size; if (size == 0) exit(0); else exit(1) }'
then
    exit_code=$exit_pass
fi

gfrm $gftmp
rm -f $localtmp
exit $exit_code
