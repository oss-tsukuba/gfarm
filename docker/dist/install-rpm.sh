#!/bin/sh
set -xeu

: ${PKG:=gfarm}

sudo rpm -Uvh ~/rpmbuild/RPMS/x86_64/$PKG-*
mkdir -p ~/local/rpms
cp -p ~/rpmbuild/RPMS/x86_64/$PKG-* ~/local/rpms
for h in c2 c3 c4
do
	echo $h
	ssh $h sudo rpm -Uvh local/rpms/$PKG-*
done
rm -rf ~/local/rpms
