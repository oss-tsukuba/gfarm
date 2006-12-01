#!/bin/sh

. ./regress.conf

trap 'gfrm $hosts_list; exit $exit_trap' $trap_sigs

hosts_list=$localtop/RT_gfdf_hosts.$$

if ! gfhost >$hosts_list || [ `cat $hosts_list | wc -l` -eq 0 ]; then
    rm -f $hosts_list
    exit $exit_unsupported
fi

if gfhost >$hosts_list &&
   gfdf -H $hosts_list | awk 'NR > 1 { print $5 }' | sort |\
     cmp $hosts_list -; then
    exit_code=$exit_pass
fi

rm -f $hosts_list
exit $exit_code
