#!/bin/sh

. ./regress.conf

ls_err=$localtop/RT_ls_err.$$

trap 'rm -f $ls_err; exit $exit_trap' $trap_sigs

LANG=C ls $hooktop/notexist 2>$ls_err

if [ $? -ne 0 ] &&
    awk '{
	if ($0 ~ /(: No such file or directory| not found)$/)
	    exit 0
	else
	    exit 1
    }' $ls_err
then
	exit_code=$exit_pass
fi

rm -f $ls_err
exit $exit_code
