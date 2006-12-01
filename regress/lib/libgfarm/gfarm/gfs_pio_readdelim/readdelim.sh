#!/bin/sh

. ./regress.conf

return_values_file=$localtop/tst_out$$
return_errmsg_file=$localtop/tst_err$$
trap 'rm -f $return_values_file $return_errmsg_file; exit $exit_trap' \
    $trap_sigs

if $testbin/readdelim $1 $2 $6 $7 1>$return_values_file 2>$return_errmsg_file; then
    to_be_read_string=$3
    to_be_returned_buf_size=$4
    to_be_read_length=$5
    IFS=:
    read < $return_values_file
    if [ $3 = $to_be_read_string ] &&
       [ $4 = $to_be_returned_buf_size ] &&
       [ $5 = $to_be_read_length ]; then
	exit_code=$exit_pass
    fi	
elif [ $? -eq 1 ] && echo "$6" | cmp - $return_errmsg_file; then
	exit_code=$exit_pass
fi

rm -f $return_values_file $return_errmsg_file
exit $exit_code
