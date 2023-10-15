#!/bin/bash

sed -i 's/^GT_PACKAGE(\[gsi-openssh\]/#GT_PACKAGE(\[gsi-openssh\]/' configure.ac
sed -i 's/^    \[globus_gss_assist\], \[gsi-openssh\])/#    \[globus_gss_assist\], \[gsi-openssh\])/' configure.ac
sed -i 's/OPENSSL_VERSION=$($PKG_CONFIG --modversion openssl)/OPENSSL_VERSION=$($PKG_CONFIG --modversion openssl11)/' configure.ac
autoconf

for f in $(find . -name configure.ac -exec grep -q 'OPENSSL' \{} \; -print)
do
        sed -i 's/PKG_CHECK_MODULES(\[OPENSSL\], \[openssl /PKG_CHECK_MODULES(\[OPENSSL\], \[openssl11 /' $f
        sed -i 's/OPENSSL_PKGCONFIG="openssl /OPENSSL_PKGCONFIG="openssl11 /' $f
        sed -i 's/AC_PATH_PROGS(OPENSSL, \[openssl\])/AC_PATH_PROGS(OPENSSL, \[openssl11\])/' $f
        sed -i 's/AC_PATH_PROGS(\[OPENSSL\], openssl)/AC_PATH_PROGS(\[OPENSSL\], openssl11)/' $f
	pushd $(dirname $f) > /dev/null
	autoconf
	popd > /dev/null
done
