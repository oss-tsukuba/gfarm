#!/bin/sh

set -eux

: $GFDOCKER_PRIMARY_USER

rpm -ivh /home/${GFDOCKER_PRIMARY_USER}/rpmbuild/RPMS/x86_64/gfarm2fs-*.rpm

# for autofs
cp $(which mount.gfarm2fs) /sbin/
