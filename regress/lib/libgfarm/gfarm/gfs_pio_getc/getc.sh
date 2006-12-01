#!/bin/sh

. ./regress.conf

return_char_file=$localtop/tst$$
trap 'rm -f $return_char_file; exit $exit_trap' $trap_sigs

if $testbin/getc $1 1>$return_char_file &&
    echo "$2" | cmp - $return_char_file; then
	exit_code=$exit_pass
fi

rm -f $return_char_file
exit $exit_code
