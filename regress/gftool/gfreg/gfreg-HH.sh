#!/bin/sh

. ./regress.conf

trap 'rm -f $hosts_list; gfrm $gftmp; exit $exit_trap' \
    $trap_sigs

hosts_list=$localtop/RT_gfreg-H_hosts.$$

if ! gfhost | head -2 >$hosts_list; then
    exit $exit_unsupported
fi

if gfreg -H $hosts_list $data/1byte $gftmp &&
   gfexport $gftmp | cmp -s - $data/1byte &&
   [ -n `gfwhere $gftmp | awk 'NR > 1 { print $2 }' | comm -12 - $hosts_list` ]
then
    exit_code=$exit_pass
fi

rm $hosts_list
gfrm $gftmp
exit $exit_code
