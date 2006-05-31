#!/bin/sh

. ./regress.conf

trap 'rm -f $localtmp; exit $exit_trap' $trap_sigs

if gfls -d / >$localtmp && cmp -s $localtmp $testbase/root.out; then
	exit_code=$exit_pass
fi

rm -f $localtmp
exit $exit_code
