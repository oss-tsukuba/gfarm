#!/bin/sh

LIST="
centos7/src
centos7/pkg
centos8/src
centos8/pkg
almalinux8/src
almalinux8/pkg
ubuntu1804/src
ubuntu2004/src
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

REULT_NAME=0
for name in ${LIST}; do
    (cd ${BASEDIR}/${name} && time make reborn && make s3setup && make s3test)
    RESULT=$?
    (cd ${BASEDIR}/${name} && make down)
    REULT_NAME=$name
    [ $RESULT -eq 0 ] || break
done

cleanup
if [ $RESULT -eq 0 ]; then
    echo "All successful"
else
    echo "Failed in ${RESULT_NAME}"
fi
exit $RESULT
