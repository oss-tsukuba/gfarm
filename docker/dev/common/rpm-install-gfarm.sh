#!/bin/sh

set -eux

: $GFDOCKER_NUM_JOBS
: $GFDOCKER_PRIMARY_USER

: ${OPENSSL_PACKAGE_NAME:=}

WITH_OPENSSL_OPT=
if [ -n "${OPENSSL_PACKAGE_NAME}" ]; then
    WITH_OPENSSL_OPT="--with-openssl=${OPENSSL_PACKAGE_NAME}"
fi

rpm -ivh /home/${GFDOCKER_PRIMARY_USER}/rpmbuild/RPMS/*/gfarm-*.rpm

GFARM_OPT=" --with-globus --enable-xmlattr --enable-xmlattr ${WITH_OPENSSL_OPT}"

# for regress
opts="--sysconfdir=/etc --prefix=/usr ${GFARM_OPT}"
su - "$GFDOCKER_PRIMARY_USER" -c \
  "cd ~/gfarm && ./configure ${opts} && make -j '${GFDOCKER_NUM_JOBS}'"
