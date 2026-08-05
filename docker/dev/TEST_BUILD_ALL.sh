#!/bin/bash

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
rockylinux9/src
rockylinux9/pkg
rockylinux10/src
rockylinux10/pkg
fedora44/src
fedora44/pkg
debian13/src
ubuntu2204/src
ubuntu2404/src
ubuntu2604/src
opensuse/src
"

BASEDIR=dist

REGRESS=${REGRESS:-0}
SKIP_PKG=${SKIP_PKG:-0}

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
    if [ $SKIP_PKG -eq 1 ] && [[ $name =~ .*"/pkg" ]]; then
        continue
    fi
    if [ $REGRESS -eq 1 ]; then
        (cd ${BASEDIR}/${name} && make reborn && make regress)
    else
        (cd ${BASEDIR}/${name} && make reborn)
    fi
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
