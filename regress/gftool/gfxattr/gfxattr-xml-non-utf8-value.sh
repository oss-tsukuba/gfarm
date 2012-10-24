#!/bin/sh

. ./regress.conf

dir=${gftmp}

attr_src=${testbase}/gfxattr-xml-non-utf8-value.src
attr_log=${localtmp}.log

attrname="user.attr"

setup() {
    gfmkdir ${dir}
    gfxattr -r -x ${dir} ${attrname}
    rm -f ${attr_log}
}

cleanup() {
    rm -f ${attr_log}
    gfxattr -r -x ${dir} ${attrname}
    gfrmdir ${dir}
}

if $regress/bin/is_xmlattr_supported; then
	:
else
	exit $exit_unsupported
fi

trap 'cleanup; exit $exit_trap' $trap_sigs

setup

gfxattr -s -c -x -f ${attr_src} ${dir} ${attrname} >${attr_log} 2>&1
if test $? -eq 0; then
    cleanup
    exit $exit_fail
fi

if grep "illegal byte sequence" ${attr_log} >/dev/null 2>&1; then
    true
else
    cleanup
    exit $exit_fail
fi

gfxattr -l -x ${dir}
if test $? -ne 0; then
    cleanup
    exit $exit_fail
fi

cleanup

exit $exit_pass
