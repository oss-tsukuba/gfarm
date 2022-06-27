#!/bin/sh

set -eux

: $GFDOCKER_PRIMARY_USER

rpm -ivh /home/${GFDOCKER_PRIMARY_USER}/rpmbuild/RPMS/*/gfarm2fs-*.rpm

# for autofs
cp $(which mount.gfarm2fs) /sbin/
