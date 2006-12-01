#!/bin/sh

. ./regress.conf

trap 'gfrm -r $gftmp; exit $exit_trap' $trap_sigs

if [ `gfhost | head -2 | wc -l` -ne 2 ]; then
    exit $exit_unsupported
fi

shost=`gfhost | sed -n '1p'`
dhost=`gfhost | sed -n '2p'`

if gfmkdir $gftmp &&
   gfreg -h $shost $data/1byte $gftmp &&
   gfreg -h $shost $data/ok.sh $gftmp &&
   gfrep -d $dhost $gftmp; then
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

gfrm -r $gftmp
exit $exit_code
