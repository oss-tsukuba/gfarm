#!/bin/sh

. ./regress.conf

if $regress/bin/am_I_gfarm_super_adm; then
    exit $exit_unsupported
elif $regress/bin/am_I_gfarmadm; then
    :
else
    exit $exit_unsupported
fi

if gfgroup -c DOES-NOT+EXIST 2>&1 | grep 'operation not permitted'
then
    exit_code=$exit_pass
fi
exit $exit_code
