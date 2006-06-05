#!/bin/sh

. ./regress.conf

ls_err=$localtop/RT_ls_err.$$

trap 'rm -f $ls_err; exit $exit_trap' $trap_sigs

LANG=C ls /gfarm/notexist 2>$ls_err

if [ $? = 1 ] &&
    awk '{
	if ($0 ~ /\/gfarm\/notexist\: No such file or directory/)
	    exit 0
    	else
	    exit 1
    }' $ls_err
then
	exit_code=$exit_pass
fi

rm -f $ls_err
exit $exit_code
