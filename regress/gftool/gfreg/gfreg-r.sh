#!/bin/sh

. ./regress.conf

trap 'rm -f $hosts_list; gfrm $gftmp; exit $exit_trap' \
    $trap_sigs

hosts_list=$localtop/RT_gfreg-r_hosts.$$
localdir=RT_gfreg-r_localdir.$$
localpath=$localtop/$localdir

if ! gfhost | head -2 >$hosts_list; then
    exit $exit_unsupported
fi

if mkdir $localpath && 
   cp $data/1byte $localpath &&
   gfmkdir $gftmp &&
   gfreg -r $localpath $gftmp &&
   gfexport $gftmp/$localdir/1byte | cmp -s - $data/1byte &&
   [ -n `gfwhere $gftmp/$localdir/1byte | awk 'NR > 1 { print $2 }' | \
	comm -12 - $hosts_list` ]; then
    exit_code=$exit_pass
fi

rm -rf $localpath $hosts_list
gfrm -rf $gftmp
exit $exit_code
