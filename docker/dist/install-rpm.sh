#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; rm -rf ~/local/rpms; exit $status' 0 1 2 15

: ${PKG:=gfarm}

sudo rpm -Uvh --force ~/rpmbuild/RPMS/x86_64/$PKG-*
mkdir -p ~/local/rpms
cp -p ~/rpmbuild/RPMS/x86_64/$PKG-* ~/local/rpms
gfarm-prun -p sudo rpm -Uvh --force local/rpms/$PKG-*

status=0
echo Done
