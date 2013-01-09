#!/bin/sh

. ./regress.conf

host=tmphostname-`hostname`-$$
port=9999
arch=tmparchname
# 129 characters (GFARM_HOST_ARCHITECTURE_NAME_MAX in gfm_proto.h)
arch2=XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

if $regress/bin/am_I_gfarmadm; then
    :
else
    exit $exit_unsupported
fi

trap 'gfhost -d $host 2>/dev/null; rm -f $localtmp; exit $exit_trap' $trap_sigs

if gfhost -c -a $arch -p $port $host; then
    if gfhost -m -a $arch2 $host 2>$localtmp; then
	:
    elif grep ': invalid argument$' $localtmp >/dev/null; then
	exit_code=$exit_pass
    fi
    gfhost -d $host
fi

rm -f $localtmp
exit $exit_code
