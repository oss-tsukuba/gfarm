#!/bin/sh

. ./regress.conf

datafile=${1?'parameter <datafile> is needed'}

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

gfhost | awk -f $testbase/search_host_and_domain.awk | while read shost domain
do
    if gfreg -h $shost $datafile $gftmp &&
	gfrep -D $domain $gftmp &&      
	gfwhere $gftmp | \
	    awk -f $testbase/check_gfwhere_out_after_-D.awk $shost $domain
    then	
	exit $exit_pass 	# exit from while loop
    else
	exit $exit_fail
    fi	
done
exit_code=$?

gfrm $gftmp
exit $exit_code
