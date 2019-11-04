#!/bin/sh

. ./regress.conf

large_num=4100

dir=${gftmp}

attr_src=${localtmp}.src
attr_got=${localtmp}.got

attrname="user.longvalue"

setup() {
    gfmkdir ${dir}
    gfxattr -r ${dir} ${attrname}
    awk 'BEGIN{printf "hoge00000"
         for (i = 1; i < '${large_num}'; i++) printf ",hoge%05d",i
         exit}' > ${attr_src}
}

cleanup() {
    gfxattr -r ${dir} ${attrname}
    gfrmdir ${dir}
    rm -f ${attr_src}
    rm -f ${attr_got}
}

trap 'cleanup; exit $exit_trap' $trap_sigs

setup

gfxattr -s -c -f ${attr_src} ${dir} ${attrname}
if test $? -ne 0; then
    cleanup
    exit $exit_fail
fi
# wait to flush the extended attribute to the backend database
sleep 2
gfxattr -g -f ${attr_got} ${dir} ${attrname}
diff -c ${attr_src} ${attr_got}
if test $? -ne 0; then
    cleanup
    exit $exit_fail
fi

gfxattr -r ${dir} ${attrname}
st=$?

cleanup

if test ${st} -eq 0; then
    exit $exit_pass
else
    exit $exit_fail
fi
