#!/bin/sh

set -eux

: $GFDOCKER_PRIMARY_USER

rpm -ivh /home/${GFDOCKER_PRIMARY_USER}/rpmbuild/RPMS/*/gfarm2fs-*.rpm

# for autofs
dst=/sbin/mount.gfarm2fs
src="$(which mount.gfarm2fs)"

if [ ! "$src" -ef "$dst" ]; then
    cp "$src" "$dst"
fi
