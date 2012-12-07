#!/bin/sh

. ./regress.conf

$regress/bin/am_I_gfarmadm
if test $? -ne 0; then
    exit $exit_unsupported
fi

cleanup() {
    for i in `gfhost`; do
	gfhostgroup -r $i > /dev/null 2>&1
    done
}

trap 'celanup; exit $exit_trap' $trap_sigs

hosts=`gfhost`

st=0
for i in ${hosts}; do
    gfhostgroup -s $i "${i}-group" > /dev/null 2>&1
    st=$?
done
if test ${st} -ne 0; then
    cleanup
    exit $exit_fail
fi

for i in ${hosts}; do
    gfhostgroup -s $i "${i}-group-mod" > /dev/null 2>&1
    st=$?
done
if test ${st} -ne 0; then
    cleanup
    exit $exit_fail
fi

st=0
for i in ${hosts}; do
    g=`gfhostgroup $i | awk '{ print $NF }'`
    if test "X${g}" != "X${i}-group-mod" -o "X${g}" = "X"; then
	st=1
    fi
done

cleanup
if test ${st} -eq 0; then
    exit $exit_pass
else
    exit $exit_fail
fi
