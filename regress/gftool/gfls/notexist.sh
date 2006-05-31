#!/bin/sh

. ./regress.conf

gfls_out=$localtop/RT_gfls_out.$$

trap 'rm -f $gfls_out; exit $exit_trap' $trap_sigs

gfls /notexist 2>$gfls_out

if [ $? = 1 ] && cmp -s $gfls_out $testbase/notexist.out; then
	exit_code=$exit_pass
fi

rm -f $gfls_out
exit $exit_code
