#!/bin/sh

. ./regress.conf

return_string_file=$localtop/tst$$
trap 'rm -f $return_string_file; exit $exit_trap' $trap_sigs

if $testbin/close $1 2>$return_string_file; then
	exit_code=$exit_pass
elif [ $? -eq 1 ] && echo "$2" | cmp - $return_string_file; then
	exit_code=$exit_pass
fi

rm -f $return_string_file
exit $exit_code
