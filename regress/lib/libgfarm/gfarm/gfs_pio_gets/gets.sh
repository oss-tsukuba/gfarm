#!/bin/sh

. ./regress.conf

got_string_file=$localtop/tst_out$$
return_errmsg_file=$localtop/tst_err$$
trap 'rm -f $got_string_file $return_errmsg_file; exit $exit_trap' $trap_sigs

if $testbin/gets $1 1>$got_string_file 2>$return_errmsg_file; then
    if echo "$2" | cmp - $got_string_file; then
	exit_code=$exit_pass
    fi	
elif [ $? -eq 1 ] && echo "$3" | cmp - $return_errmsg_file; then
    exit_code=$exit_pass
fi

rm -f $got_string_file $return_errmsg_file
exit $exit_code
