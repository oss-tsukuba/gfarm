#!/bin/sh

. ./regress.conf

$regress/bin/am_I_gfarmadm
if test $? -ne 0; then
    exit $exit_unsupported
fi

host=dummy-host-`uname -n | awk -F. '{ print $1 }'`.$$

cleanup() {
    gfhost -d ${host} > /dev/null 2>&1
}

trap 'cleanup; exit $exit_trap' $trap_sigs

gfhost -c -a dummy -p 12345 ${host} > /dev/null 2>&1
if test $? -ne 0; then
    cleanup
    exot $exit_fail
fi

gfhostgroup -s ${host} "${host}-group" > /dev/null 2>&1
if test $? -ne 0; then
    cleanup
    exit $exit_fail
fi

gfhostgroup -s ${host} "${host}-group-mod" > /dev/null 2>&1
if test $? -ne 0; then
    cleanup
    exit $exit_fail
fi

st=0
g=`gfhostgroup ${host} | awk '{ print $NF }'`
if test $? -ne 0 -o "X${g}" != "X${host}-group-mod" -o "X${g}" = "X"; then
    st=1
fi

cleanup
if test ${st} -eq 0; then
    exit $exit_pass
else
    exit $exit_fail
fi
