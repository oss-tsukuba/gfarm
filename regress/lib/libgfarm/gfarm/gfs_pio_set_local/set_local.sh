#!/bin/sh

. ./regress.conf

tmpfile=$localtop/tst$$
trap 'rm -f $tmpfile; exit $exit_trap' $trap_sigs

if $testbin/set_local $1 $2 2>$tmpfile; then
	exit_code=$exit_pass
elif [ $? -eq 1 ] && echo "$3" | cmp - $tmpfile; then
	exit_code=$exit_pass
fi

rm -f $tmpfile
exit $exit_code
