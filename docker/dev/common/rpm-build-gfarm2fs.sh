#!/bin/sh

set -eux

cd

name=gfarm2fs
spec=${name}/gfarm2fs.spec

mkdir -p rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPEC,SPECS,SRPMS}

ver=$(grep '^Version:' ${spec} | awk '{print $2}')

name_ver=${name}-${ver}
targz=${name_ver}.tar.gz
cp -a ${name} rpmbuild/SOURCES/${name_ver}  # "mv" is too slow.

(cd rpmbuild/SOURCES &&
  tar --exclude=.svn --exclude=.git --owner=root --group=root -zcvf \
    ${targz} ${name_ver})

rpmbuild -bs ${spec}
srpm="rpmbuild/SRPMS/${name_ver}-*.src.rpm"
rpmbuild --rebuild ${srpm}
