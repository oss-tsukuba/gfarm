#!/bin/sh

. ./regress.conf

trap 'gfrm -r $gftmp; exit $exit_trap' $trap_sigs

gfhost | awk -f $testbase/search_host_and_domain.awk | while read shost domain
do
   if gfmkdir $gftmp &&
      gfreg -h $shost $data/1byte $gftmp &&
      gfreg -h $shost $data/ok.sh $gftmp &&
      gfrep -D $domain $gftmp
   then
       for f in 1byte ok.sh; do
	   if ! gfwhere $gftmp/$f | \
	      awk -f $testbase/check_gfwhere_out_after_-D.awk $shost $domain
	   then
	       exit $exit_fail	# exit from while loop
	   fi
       done
       exit $exit_pass
   else
       exit $exit_fail
   fi
done
exit_code=$?

gfrm -r $gftmp
exit $exit_code
