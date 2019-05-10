#!/bin/sh

set -eux

cd

name=gfarm
spec=${name}/package/redhat/gfarm.spec

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
GFARM_CONFIGURE_OPTION='--with-globus --enable-xmlattr' \
  rpmbuild --rebuild ${srpm}
