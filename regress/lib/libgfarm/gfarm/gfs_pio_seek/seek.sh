#!/bin/sh

. ./regress.conf

return_position_file=$localtop/tst_out$$
return_string_file=$localtop/tst_err$$

trap 'rm -f $return_position_file $return_string_file; exit $exit_trap' $trap_sigs

if $testbin/seek $1 $2 $3 $4 1>$return_position_file 2>$return_string_file; then
    if [ $4 != RETURN_RESULT ] || echo "$5" | cmp - $return_position_file; then
	exit_code=$exit_pass
    fi
elif [ $? -eq 1 ] && echo "$5" | cmp - $return_string_file; then
    exit_code=$exit_pass
fi

rm -f $return_position_file $return_string_file
exit $exit_code
