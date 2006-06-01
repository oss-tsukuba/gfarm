#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

shost=`gfhost | sed -n '1p'`
dhost=`gfhost | sed -n '2p'`

if gfreg -h $shost $data/1byte $gftmp &&
   gfrep -d $dhost $gftmp && 
   gfwhere $gftmp | awk '
      { if ($2 == "'$shost'" && $3 == "'$dhost'"  || 
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
