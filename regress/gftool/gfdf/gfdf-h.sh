#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

hostname="`gfhost | sed -n 1p`" 

if [ -z $hostname ]; then
    exit $exit_unsupported
fi

if [ `gfdf -h $hostname | awk 'NR > 1 { print $5 }'` = $hostname ]; then
    exit_code=$exit_pass
fi

exit $exit_code
