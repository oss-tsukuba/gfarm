#!/bin/sh

. ./regress.conf

trap 'rm -f $hosts_list $return_errmsg; gfrm -r $gftmp; exit $exit_trap' $trap_sigs

data_file=`echo $datafile | sed s:/:_:g`
hosts_list=$localtop/RT_gfrep-H_"$data_file"_hosts.$$
return_errmsg=$localtop/RT_gfrep-H_"$data_file"_err.$$

if ! gfhost | head -2 >$hosts_list || [ `cat $hosts_list | wc -l` -ne 2 ]; then
    rm -f $hosts_list
    exit $exit_unsupported
fi

shost=`cat $hosts_list | sed -n '1p'`
dhost=`cat $hosts_list | sed -n '2p'`

if gfmkdir $gftmp &&
   gfreg -h $shost $data/1byte $gftmp &&
   gfreg -h $shost $data/ok.sh $gftmp &&
   echo $dhost | gfrep -H - $gftmp 2>$return_errmsg; then
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
elif [ "`cat $return_errmsg | awk '{ printf \"%s %s %s\", $3, $4, $5 }'`" = \
	"operation not permitted" ]; then
    exit_code=$exit_xfail
fi

rm -f $hosts_list $return_errmsg
gfrm -r $gftmp
exit $exit_code
