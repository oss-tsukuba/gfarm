#!/bin/sh

. ./regress.conf

gfwhere_out=$localtop/RT_gfwhere_out.$$
host=$localtop/RT_gfwhere_host.$$

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

#trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

if gfreg $datafile $gftmp &&
   gfwhere $gftmp >$gfwhere_out &&
   awk '{print $NF}' $gfwhere_out >$host
then
	if [ -x $datafile ]; then
		if gfhost -M "`cat $host`" | awk '{printf "%s: %s\n", $1, $3}' | cmp -s - $gfwhere_out
		then
			exit_code=$exit_pass
		fi
	else
		if echo "0: `cat $host`" | cmp -s - $gfwhere_out
		then
			exit_code=$exit_pass
		fi	
	fi
fi

#rm -f $gfwhere_out $host
exit $exit_code
