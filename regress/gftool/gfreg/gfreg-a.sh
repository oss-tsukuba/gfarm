#!/bin/sh

. ./regress.conf

trap 'rm -f $hosts_list; gfrm $gftmp; exit $exit_trap' $trap_sigs

hosts_list=$localtop/RT_gfreg-a_hosts.$$

if ! gfhost | head -2 >$hosts_list; then
    exit $exit_unsupported
fi

if arch=`gfhost -M | sed -n 1p | awk '{ print $1 }'` &&
   gfreg -a $arch $data/ok.sh $gftmp &&
   gfchmod 644 $gftmp &&
   gfexport $gftmp | cmp - $data/ok.sh &&
   [ -n `gfwhere $gftmp | awk 'NR > 1 { print $2 }' | comm -12 - $hosts_list` ]
then
    exit_code=$exit_pass
fi

rm -f $hosts_list
gfrm $gftmp
exit $exit_code
