#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; rm -rf ~/local/rpms; exit $status' 0 1 2 15

: ${PKG:=gfarm}

sudo rpm -Uvh --force ~/rpmbuild/RPMS/x86_64/$PKG-*
mkdir -p ~/local/rpms
cp -p ~/rpmbuild/RPMS/x86_64/$PKG-* ~/local/rpms
for h in c2 c3 c4
do
	echo $h
	ssh $h sudo rpm -Uvh --force local/rpms/$PKG-*
done
status=0
echo Done
