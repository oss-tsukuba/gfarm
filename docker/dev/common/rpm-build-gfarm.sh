#!/bin/sh

set -eux

: ${OPENSSL_PACKAGE_NAME:=}

GFDOCKER_SCRIPT_PATH="`dirname $0`"

name=gfarm
srcdir=/work/${name}
spec=${srcdir}/package/redhat/${name}.spec

WITH_OPENSSL_OPT=
if [ -n "${OPENSSL_PACKAGE_NAME}" ]; then
    WITH_OPENSSL_OPT="--with-openssl=${OPENSSL_PACKAGE_NAME}"
fi

ver=$(grep "^%define ver" ${spec} | awk '{print $3}')

# do not set $GFARM_CONFIGURE_OPTION here, because it makes "rpmbuild -bs"
# create a wrong SRPM name. i.e. gfarm-gsi-${ver} instead of gfarm-${ver}
# instead, use -e option to specify $GFARM_CONFIGURE_OPTION at "--rebuild".

"${GFDOCKER_SCRIPT_PATH}/rpm-build.sh" \
	-d "${srcdir}" \
	-e "GFARM_CONFIGURE_OPTION=
		--with-globus --enable-xmlattr ${WITH_OPENSSL_OPT}" \
	-s "${spec}" \
	-v "${ver}" \
	"${name}"
