#!/bin/sh

. ./regress.conf

case $# in
2)  mask=$1
    mode=$2;;
*)  echo "Usage: $0 <mask> <mode printed under 'ls -l'>" >&2
    exit $exit_fail;;
esac

trap 'rm -f $hooktmp; exit $exit_trap' $trap_sigs

if umask $mask &&
    touch $hooktmp &&
    [ x"`ls -l $hooktmp | awk '{ print $1 }'`" = x$mode ]; then 
    exit_code=$exit_pass
fi

rm -f $hooktmp
exit $exit_code
