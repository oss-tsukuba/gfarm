#!/bin/sh

. ./regress.conf

trap 'rm -f $hosts_list; gfrm -r $gftmp; exit $exit_trap' $trap_sigs

hosts_list=$localtop/RT_gfrep-D_dir_hosts.$$

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

if gfmkdir $gftmp &&
   gfreg -h $shost $data/1byte $gftmp &&
   gfreg -h $shost $data/ok.sh $gftmp &&
   gfrep -D $domain $gftmp; then
    exit_code=$exit_pass
    for f in 1byte ok.sh; do
	if ! gfwhere $gftmp/$f | $awk '
	    NR > 1 {
		if ($2 == "'$shost'" && $3 == "'$dhost'" ||
		    $2 == "'$dhost'" && $3 == "'$shost'") 
		    exit 0
		else
		    exit 1	
	    }'
	then
	    exit_code=$exit_fail 
	    break;
	fi
    done 
fi

rm -f $hosts_list
gfrm -r $gftmp
exit $exit_code
