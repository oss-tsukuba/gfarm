#!/bin/sh

LIST="
centos7/src
centos7/pkg
centos8/src
centos8/pkg
centos9/src
almalinux8/src
almalinux8/pkg
rockylinux8/src
rockylinux8/pkg
fedora33/src
opensuse/src
opensuse/pkg
ubuntu1804/src
ubuntu2004/src
debian10/src
debian11/src
"

BASEDIR=dist

cleanup() {
    echo "cleanup"
    for name in ${LIST}; do
        (cd ${BASEDIR}/${name} && make down)
    done
}

trap_sigs='1 2 15'
trap 'cleanup; exit 1' $trap_sigs

RESULT_NAME=0
for name in ${LIST}; do
    (cd ${BASEDIR}/${name} && time make reborn)
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
