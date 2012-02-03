#!/bin/sh

. ./regress.conf

trap 'rm -f $localtmp; exit $exit_trap' $trap_sigs

gfls gfarm://unknown > $localtmp 2>&1
r=$?
mv $localtmp $localtmp.0
tail -n1 $localtmp.0 > $localtmp
rm $localtmp.0

if [ $r = 1 ] && cmp -s $localtmp $testbase/unknownhost.out; then
	exit_code=$exit_pass
else
	cat $localtmp
fi

rm -f $localtmp
exit $exit_code
