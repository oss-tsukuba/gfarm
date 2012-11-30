#!/bin/sh

. ./regress.conf

empty_acl=`dirname $testbin`/empty_acl/empty_acl

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

if gfreg $data/1byte $gftmp &&
   $empty_acl -a $gftmp
then
    exit_code=$exit_pass
fi

gfrm -f $gftmp
exit $exit_code
