#!/bin/sh

. ./regress.conf

empty_acl=`dirname $testbin`/empty_acl/empty_acl

trap 'gfrmdir $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp &&
   $empty_acl -d $gftmp
then
    exit_code=$exit_pass
fi

gfrmdir $gftmp
exit $exit_code
