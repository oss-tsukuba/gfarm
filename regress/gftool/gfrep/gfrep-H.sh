#!/bin/sh

. ./regress.conf

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'rm -f $hosts_list $return_errmsg; gfrm $gftmp; exit $exit_trap' $trap_sigs

data_file=`echo $datafile | sed s:/:_:g`
hosts_list=$localtop/RT_gfrep-H_"$data_file"_hosts.$$
return_errmsg=$localtop/RT_gfrep-H_"$data_file"_err.$$

if ! gfhost | head -2 >$hosts_list || [ `cat $hosts_list | wc -l` -ne 2 ]; then
    rm -f $hosts_list
    exit $exit_unsupported
fi

shost=`cat $hosts_list | sed -n '1p'`
dhost=`cat $hosts_list | sed -n '2p'`

if gfreg -h $shost $datafile $gftmp &&
   echo $dhost | gfrep -H - $gftmp 2>$return_errmsg && 
   gfwhere $gftmp | $awk '
      NR > 1 {
        if ($2 == "'$shost'" && $3 == "'$dhost'"  ||
	    $2 == "'$dhost'" && $3 == "'$shost'") 
	   exit 0
	else
	   exit 1
      }'; then
    exit_code=$exit_pass
elif [ "`cat $return_errmsg | awk '{ printf \"%s %s %s\", $3, $4, $5 }'`" = \
	"operation not permitted" ]; then
    exit_code=$exit_xfail
fi

rm -f $hosts_list $return_errmsg
gfrm $gftmp
exit $exit_code
