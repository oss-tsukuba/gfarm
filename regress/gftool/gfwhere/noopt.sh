#!/bin/sh

. ./regress.conf

gfwhere_out=$localtop/RT_gfwhere_out.$$
thishost=$localtop/RT_gfwhere_thishost.$$

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'gfrm -f $gftmp; rm -f $gfwhere_out $thishost; exit $exit_trap' $trap_sigs

if gfreg $datafile $gftmp &&
   gfwhere $gftmp | sed -n '$p' >$gfwhere_out
then
	host=`awk '{print $NF}' $gfwhere_out`
	if [ -x $datafile ]; then
		gfhost -M `hostname` >$thishost 2>/dev/null
		if [ -s $thishost ]; then
			arch=`cat $thishost | awk '{printf "%s\n", $1}'`
		else
			arch=noarch
		fi
		if echo "${arch}: $host" | cmp -s - $gfwhere_out
		then
			exit_code=$exit_pass
		fi
	else
		if echo "0: $host" | cmp -s - $gfwhere_out
		then
			exit_code=$exit_pass
		fi	
	fi
fi

gfrm -f $gftmp
rm -f $gfwhere_out $thishost
exit $exit_code
