#!/bin/sh

. ./regress.conf

gfls_out=$localtop/RT_gfls_out.$$

trap 'rm -f $gfls_out; exit $exit_trap' $trap_sigs

if gfls -d / >$gfls_out && cmp -s $gfls_out $testbase/root.out; then
	exit_code=$exit_pass
fi

rm -f $gfls_out
exit $exit_code
