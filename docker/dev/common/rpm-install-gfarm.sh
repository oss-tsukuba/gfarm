#!/bin/sh

set -eux

: $GFDOCKER_NUM_JOBS
: $GFDOCKER_PRIMARY_USER

rpm -ivh /home/${GFDOCKER_PRIMARY_USER}/rpmbuild/RPMS/x86_64/gfarm-*.rpm

# for regress
opts="--sysconfdir=/etc --prefix=/usr --with-globus --enable-xmlattr"
su - "$GFDOCKER_PRIMARY_USER" -c \
  "cd ~/gfarm && ./configure ${opts} && make -j '${GFDOCKER_NUM_JOBS}'"
