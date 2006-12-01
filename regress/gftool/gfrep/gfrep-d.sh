#!/bin/sh

. ./regress.conf

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

if [ `gfhost | head -2 | wc -l` -ne 2 ]; then
    exit $exit_unsupported
fi

shost=`gfhost | sed -n '1p'`
dhost=`gfhost | sed -n '2p'`

if gfreg -h $shost $datafile $gftmp &&
   gfrep -d $dhost $gftmp && 
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

gfrm $gftmp
exit $exit_code
