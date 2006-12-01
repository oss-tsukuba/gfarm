#!/bin/sh

. ./regress.conf

trap 'gfrm -r $gftmp; exit $exit_trap' $trap_sigs

if [ `gfhost | head -2 | wc -l` -ne 2 ]; then
    exit $exit_unsupported
fi

if gfmkdir $gftmp &&
   gfreg $data/1byte $gftmp &&
   gfreg $data/ok.sh $gftmp &&
   gfrep -N 2 $gftmp; then
    exit_code=$exit_pass   
    for f in 1byte ok.sh; do
	if ! gfwhere $gftmp/$f | \
	   awk 'NR > 1 { if (NF == 3) exit 0; else exit 1}'; then
	    exit_code=$exit_fail 
	    break;
	fi    
    done	
fi

gfrm -r $gftmp
exit $exit_code
