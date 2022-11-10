#!/bin/bash

LIST_OLD_SYSTEMD="
centos7/src
centos7/pkg
"
LIST="
centos8/src
centos8/pkg
centos9/src
centos9/pkg
almalinux8/src
almalinux8/pkg
almalinux9/src
almalinux9/pkg
rockylinux8/src
rockylinux8/pkg
fedora33/src
opensuse/src
opensuse/pkg
ubuntu1804/src
ubuntu2004/src
ubuntu2204/src
debian10/src
debian11/src
"

BASEDIR=dist
IS_CGROUP_V2_COMMAND="./common/is_cgroup_v2.sh"

if ${IS_CGROUP_V2_COMMAND}; then
    echo "unsupported: ${LIST_OLD_SYSTEMD}"
    LIST_OLD_SYSTEMD=""
fi
LIST_ALL="${LIST_OLD_SYSTEMD} ${LIST}"

cleanup() {
    echo "cleanup"
    for name in ${LIST_ALL}; do
        (cd ${BASEDIR}/${name} && make down)
    done
}

trap_sigs='1 2 15'
trap 'cleanup; exit 1' $trap_sigs

RESULT_NAME=0
for name in ${LIST_ALL}; do
    (cd ${BASEDIR}/${name} && make reborn)
    RESULT=$?
    (cd ${BASEDIR}/${name} && make down)
    RESULT_NAME=$name
    [ $RESULT -eq 0 ] || break
done

if [ $RESULT -eq 0 ]; then
    cleanup
    echo "All successful"
else
    echo "Failed in ${RESULT_NAME}"
fi
exit $RESULT
