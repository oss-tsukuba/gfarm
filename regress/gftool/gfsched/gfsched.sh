#!/bin/sh

. ./regress.conf

trap 'rm $hosts_list; gfrm $gftmp; exit $exit_trap' $trap_sigs

hosts_list=$localtop/RT_gfsched_hosts.$$

if gfreg $data/0byte $data/1byte $gftmp &&
   gfsched $gftmp | sort >$hosts_list &&
   gfwhere $gftmp | awk 'NR > 1 { print $2 }' | sort | cmp $hosts_list -; then
	exit_code=$exit_pass
fi

rm $hosts_list
gfrm $gftmp
exit $exit_code
