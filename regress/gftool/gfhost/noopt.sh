#!/bin/sh

. ./regress.conf

hosts_meta=$localtop/RT_gfhost_hosts.$$
not_hosts_meta=$localtop/RT_gfhost_not_hosts.$$

trap 'rm -f $hosts_meta $not_hosts_meta ; exit $exit_trap' $trap_sigs

if gfhost >$hosts_meta && [ -s $hosts_meta ] &&
   gfhost -M | awk '{ print $3 }' | sort |
	comm -13 - $hosts_meta >$not_hosts_meta &&
   [ ! -s $not_hosts_meta ] 
then
	exit_code=$exit_pass
fi

rm -f $hosts_meta $not_hosts_meta
exit $exit_code
