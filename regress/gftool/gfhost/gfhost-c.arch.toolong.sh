#!/bin/sh

. ./regress.conf

host=tmphostname-`hostname`-$$
port=9999
# 129 characters (GFARM_HOST_ARCHITECTURE_NAME_MAX in gfm_proto.h)
arch=XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

if $regress/bin/am_I_gfarmadm; then
    :
else
    exit $exit_unsupported
fi

trap 'gfhost -d $host 2>/dev/null; rm -f $localtmp; exit $exit_trap' $trap_sigs

if gfhost -c -a $arch -p $port $host 2>$localtmp; then
    gfhost -d $host
else
    if grep ': invalid argument$' $localtmp >/dev/null; then
	exit_code=$exit_pass
    fi
fi

rm -f $localtmp
exit $exit_code
