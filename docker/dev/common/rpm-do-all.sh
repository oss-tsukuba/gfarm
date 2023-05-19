#!/bin/sh

set -eux

# NOTE:
# rpm-build-gfarm.sh and rpm-install-gfarm.sh are using OPENSSL_PACKAGE_NAME
# which may be set by *-base-Dockerfile (e.g. centos7-base-Dockerfile)
# : $OPENSSL_PACKAGE_NAME

# this and rpm-install*.sh scripts are using GFDOCKER_PRIMARY_USER
: $GFDOCKER_NUM_JOBS
: $GFDOCKER_PRIMARY_USER
: $GFDOCKER_TENANT_ADMIN_USER

GFDOCKER_SCRIPT_PATH="`dirname $0`"

cd

for pkg in \
	gfarm \
	gfarm2fs \
	jwt-logon \
	jwt-agent \
	scitokens-cpp \
	cyrus-sasl-xoauth2-idp
do
  su - "${GFDOCKER_PRIMARY_USER}" \
    "${GFDOCKER_SCRIPT_PATH}/rpm-build-${pkg}.sh" || {
      echo >&2 "$0: error during rpm-build-${pkg}"
      exit 1
    }

  "${GFDOCKER_SCRIPT_PATH}/rpm-install-${pkg}.sh" || {
    echo >&2 "$0: error during rpm-install-${pkg}"
    exit 1
  }
done

mandb
