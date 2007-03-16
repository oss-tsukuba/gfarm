#!/bin/sh

. ./regress.conf

return_errmsg_file=$localtop/tst_err$$
trap 'rm -f $return_errmsg_file; exit $exit_trap' $trap_sigs

if $1 $2 $3 2>$return_errmsg_file; then
    if [ "`gfexport $2`" = "$3" ]; then
	exit_code=$exit_pass
    fi
elif [ $? -eq 1 ] && echo "$4" | cmp - $return_errmsg_file; then
    exit_code=$exit_pass
fi

rm -f $return_errmsg_file
exit $exit_code
