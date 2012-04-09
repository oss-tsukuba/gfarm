#!/bin/sh

. ./regress.conf

dir=/tmp/gfrepattr-test.$$
syml=/tmp/syml-gfrepattr-test.$$
pat="g0:1, g1:1, g0:2, g2:1, g0:3"
g0s=g0:6
g1s=g1:1
g2s=g2:1

setup() {
    gfmkdir ${dir}
    gfln -s ${dir} ${syml}
    gfrepattr -r ${dir} > /dev/null 2>&1
    gfrepattr -r -h ${syml} > /dev/null 2>&1
}

cleanup() {
    gfrepattr -r ${dir} > /dev/null 2>&1
    gfrepattr -r -h ${syml} > /dev/null 2>&1
    gfrm -f ${syml}
    gfrmdir ${dir}
}

trap 'cleanup; exit $exit_trap' $trap_sigs

setup

# set attr to syml, 
gfrepattr -s -h ${syml} "${pat}" > /dev/null 2>&1
st=$?

cleanup

if test ${st} -eq 0; then
    exit $exit_xpass
else
    exit $exit_xfail
fi

