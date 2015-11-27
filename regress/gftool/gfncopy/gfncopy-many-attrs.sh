#!/bin/sh

. ./regress.conf

# XXX
exit $exit_unsupported

large_num=4100

dir=${gftmp}

pat="`awk 'BEGIN{printf "hoge00000:2"; for (i = 1; i < '${large_num}'; i++) printf ",hoge%05d:2",i; exit}'`"

setup() {
    gfmkdir ${dir}
    gfncopy -r ${dir}
}

cleanup() {
    gfncopy -r ${dir}
    gfrmdir ${dir}
    rm -f ${localtmp}
}

trap 'cleanup; exit $exit_trap' $trap_sigs

setup

gfncopy -C -S "${pat}" ${dir}
if [ $? -ne 0 ]; then
    cleanup
    exit $exit_fail
fi

gfncopy ${dir} | tr , '\012' | sort >$localtmp

awk '
BEGIN{for (i = 0; i < '${large_num}'; i++) printf "hoge%05d:2\n",i; exit}
' | diff -c - $localtmp
if [ $? -ne 0 ]; then
    cleanup
    exit $exit_fail
fi

gfncopy -r ${dir}
st=$?

cleanup

if [ ${st} -eq 0 ]; then
    exit $exit_pass
else
    exit $exit_fail
fi
