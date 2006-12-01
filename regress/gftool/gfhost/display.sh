#!/bin/sh

. ./regress.conf

hosts_meta=$localtop/RT_gfhost_hosts.$$
not_hosts_meta=$localtop/RT_gfhost_not_hosts.$$

case $# in
2)      option=$1
	field_number=$2;; # of hostname
*)	echo "Usage: $0 <option> <field_number>" >&2
	exit $exit_fail;;
esac

trap 'rm -f $hosts_meta $not_hosts_meta ; exit $exit_trap' $trap_sigs

if gfhost $option | awk '{ print $'$field_number' }' |
	sed s/\(..*\)// >$hosts_meta && [ -s $hosts_meta ] &&
   gfhost -M | awk '{ print $3 }' | sort |
	comm -13 - $hosts_meta >$not_hosts_meta &&
   [ ! -s $not_hosts_meta ] 
then
	exit_code=$exit_pass
fi

rm -f $hosts_meta $not_hosts_meta
exit $exit_code
