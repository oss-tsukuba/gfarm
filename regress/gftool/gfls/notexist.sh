#!/bin/sh

. ./regress.conf

trap 'rm -f $localtmp; exit $exit_trap' $trap_sigs
trap 'rm -f $localtmp; exit $exit_code' 0

gfls /notexist 2>$localtmp

if [ $? = 1 ] && cmp -s $localtmp $testbase/notexist.out; then
	exit_code=$exit_pass
fi
