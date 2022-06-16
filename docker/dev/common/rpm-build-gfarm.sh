#!/bin/sh

set -eux

: ${OPENSSL_PACKAGE_NAME:=}

cd

name=gfarm
spec=${name}/package/redhat/gfarm.spec

WITH_OPENSSL_OPT=
if [ -n "${OPENSSL_PACKAGE_NAME}" ]; then
    WITH_OPENSSL_OPT="--with-openssl=${OPENSSL_PACKAGE_NAME}"
fi

mkdir -p rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPEC,SPECS,SRPMS}

ver=$(grep "^%define ver" ${spec} | awk '{print $3}')

name_ver=${name}-${ver}
targz=${name_ver}.tar.gz
cp -a ${name} rpmbuild/SOURCES/${name_ver}  # "mv" is too slow.

(cd rpmbuild/SOURCES/ &&
  tar --exclude=.svn --exclude=.git --owner=root --group=root -zcvf \
    ${targz} ${name_ver})

rpmbuild -bs ${spec}
srpm="rpmbuild/SRPMS/${name_ver}-*.src.rpm"
GFARM_CONFIGURE_OPTION="--with-globus --enable-xmlattr ${WITH_OPENSSL_OPT}" \
  rpmbuild --rebuild ${srpm}
