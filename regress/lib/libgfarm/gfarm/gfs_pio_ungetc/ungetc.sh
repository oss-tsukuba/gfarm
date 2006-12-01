#!/bin/sh

. ./regress.conf

return_value_file=$localtop/tst$$
trap 'rm -f $return_value_file; exit $exit_trap' $trap_sigs

if $testbin/ungetc $1 $2 1>$return_value_file; then
    if [ $2 = "NOT_FOLLOW_GETC" ] && echo -1 | cmp - $return_value_file; then
	exit_code=$exit_pass
    fi	
fi

rm -f $return_value_file
exit $exit_code
