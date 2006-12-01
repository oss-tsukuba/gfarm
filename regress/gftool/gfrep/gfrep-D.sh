#!/bin/sh

. ./regress.conf

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'rm -f $hosts_list; gfrm $gftmp; exit $exit_trap' $trap_sigs

hosts_list=$localtop/RT_gfrep-D_hosts.$$

if ! gfhost | head -2 >$hosts_list || [ `cat $hosts_list | wc -l` -ne 2 ]; then
    rm -f $hosts_list
    exit $exit_unsupported
fi

pat='[^.][^.]*\.'

if shost=`cat $hosts_list | sed -n '1p'` &&
   dhost=`cat $hosts_list | sed -n '2p'` &&
   domain=`echo $dhost | sed -n s/$pat//p` && [ -z $domain ] &&
   shost=`cat $hosts_list | sed -n '2p'` &&
   dhost=`cat $hosts_list | sed -n '1p'` &&
   domain=`echo $dhost | sed -n s/$pat//p` && [ -z $domain ]; then
    rm -f $hosts_list
    exit $exit_unsupported
fi

if gfreg -h $shost $datafile $gftmp &&
   gfrep -D $domain $gftmp && 
   gfwhere $gftmp | $awk '
      NR > 1 {
        if ($2 == "'$shost'" && $3 == "'$dhost'"  || 
	    $2 == "'$dhost'" && $3 == "'$shost'") 
	   exit 0
	else
	   exit 1
      }'
then
	exit_code=$exit_pass
fi

rm -f $hosts_list
gfrm $gftmp
exit $exit_code
