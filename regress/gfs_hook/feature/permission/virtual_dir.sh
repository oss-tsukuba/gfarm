#!/bin/sh

. ./regress.conf

trap 'rm -rf $hooktmp; exit $exit_trap' $trap_sigs

if mkdir $hooktmp && chmod -w $hooktop
then
    if echo foo >$hooktmp/bar
    then
	exit_code=$exit_pass
    else
	exit_code=$exit_xfail
    fi	
fi

chmod +w $hooktop
rm -rf $hooktmp
exit $exit_code
