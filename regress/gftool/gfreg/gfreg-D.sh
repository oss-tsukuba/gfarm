#!/bin/sh

. ./regress.conf

trap 'rm -f $hosts_list; gfrm $gftmp; exit $exit_trap' $trap_sigs

hosts_list=$localtop/RT_gfreg-D_hosts.$$

if ! gfhost | head -2 >$hosts_list; then
    rm -f $hosts_list
    exit $exit_unsupported
fi

pat='[^.][^.]*\.'

if domain=`cat $hosts_list | sed -n 1s/$pat//p` && [ -z $domain ] &&
   domain=`cat $hosts_list | sed -n 2s/$pat//p` && [ -z $domain ]; then
    rm -f $hosts_list
    exit $exit_unsupported
fi

if gfreg -D $domain $data/1byte $gftmp &&
   gfexport $gftmp | cmp -s - $data/1byte &&
   [ -n `gfwhere $gftmp | awk 'NR > 1 { print $2 }' | comm -12 - $hosts_list` ]
then
    exit_code=$exit_pass
fi

rm -f $hosts_list
gfrm $gftmp
exit $exit_code
