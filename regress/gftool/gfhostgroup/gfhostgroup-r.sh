#!/bin/sh

. ./regress.conf

$regress/bin/am_I_gfarmadm
if test $? -ne 0; then
    exit $exit_unsupported
fi

st=0
for i in `gfhost`; do
    gfhostgroup -r $i > /dev/null 2>&1
    st=$?
done
if test ${st} -eq 0; then
    exit $exit_pass
else
    exit $exit_fail
fi
