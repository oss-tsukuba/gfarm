#!/bin/sh

. ./regress.conf

large_num=4100

dir=${gftmp}

pat="`awk 'BEGIN{printf "hoge00000:2"; for (i = 1; i < '${large_num}'; i++) printf ",hoge%05d:2",i; exit}'`"

setup() {
    gfmkdir ${dir}
    gfrepattr -r ${dir}
}

cleanup() {
    gfrepattr -r ${dir}
    gfrmdir ${dir}
    rm -f ${localtmp}
}

trap 'cleanup; exit $exit_trap' $trap_sigs

setup

gfrepattr -s -c ${dir} "${pat}"
if test $? -ne 0; then
    cleanup
    exit $exit_fail
fi

gfrepattr ${dir} | tr , '\012' | sed -e "1s/^.*'//" -e "\$s/'$//" |
	sort >$localtmp
awk '
BEGIN{for (i = 0; i < '${large_num}'; i++) printf "hoge%05d:2\n",i; exit}
' | diff -c - $localtmp
if test $? -ne 0; then
    cleanup
    exit $exit_fail
fi

gfrepattr -r ${dir}
st=$?

cleanup

if test ${st} -eq 0; then
    exit $exit_pass
else
    exit $exit_fail
fi
