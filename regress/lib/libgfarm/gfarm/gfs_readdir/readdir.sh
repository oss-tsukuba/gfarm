#!/bin/sh

. ./regress.conf

return_string_file=$localtop/tst_out$$
return_errmsg_file=$localtop/tst_err$$
trap 'rm -f $return_string_file $return_errmsg_file; exit $exit_trap' $trap_sigs

if $testbin/readdir $1 1>$return_string_file 2>$return_errmsg_file; then
    if [ `cat $return_string_file` = $2 ]; then
	exit_code=$exit_pass
    fi
elif [ $? -eq 1 ] && echo "$3" | cmp - $return_errmsg_file; then
    exit_code=$exit_pass
fi

rm -f $return_string_file $return_errmsg_file
exit $exit_code
