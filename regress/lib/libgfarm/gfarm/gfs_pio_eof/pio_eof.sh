#!/bin/sh

. ./regress.conf

return_value_file=$localtop/tst$$
trap 'rm -f $return_value_file; exit $exit_trap' $trap_sigs

if $testbin/pio_eof $1 $2 1>$return_value_file; then
    if echo 0 | cmp - $return_value_file; then
	if [ $3 != "EOF" ]; then
	    exit_code=$exit_pass
    	fi    
    else	
	if [ $3 == "EOF" ]; then
	    exit_code=$exit_pass
    	fi    
    fi	
fi

rm -f $return_value_file
exit $exit_code
