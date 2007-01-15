#!/bin/sh

# remove a filesystem node, and re-add the node.
# then see whether a replica which had refered the host is still usable or not.

. ./regress.conf

datafile=$data/1byte
gfwhere_out=$localtop/gfwhere_out.$$

trap 'gfrm -f $gftmp; rm -f $gfwhere_out; exit $exit_trap' $trap_sigs

if gfreg $datafile $gftmp &&
	gfwhere $gftmp >$gfwhere_out
then
	host=`sed -n '$p' $gfwhere_out | awk '{print $NF}'`

	# XXX we cannot run this test in parallel.
	save=`gfhost -M $host`
	gfhost -d $host
	echo $save | gfhost -R

	if gfwhere $gftmp | cmp -s - $gfwhere_out; then
		exit_code=$exit_pass
	fi
fi

gfrm -f $gftmp
rm -f $gfwhere_out
exit $exit_code
