#!/bin/sh

set -eux

BASEDIR=$(dirname $(realpath $0))
FUNCTIONS=${BASEDIR}/functions.sh
. ${FUNCTIONS}

save_package "/home/user1/rpmbuild/SRPMS/*"
save_package "/home/user1/rpmbuild/RPMS/*/*"
