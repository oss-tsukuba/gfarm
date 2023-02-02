#!/bin/sh

set -eux

GFDOCKER_SCRIPT_PATH="`dirname $0`"
name=cyrus-sasl-xoauth2-idp
srcdir=/work/gfarm/${name}
specpath="${name}.spec"

# OpenSUSE does not have cyrus-sasl-lib RPM
if type lsb_release >/dev/null &&
   [ "$(lsb_release -i | awk '{print $NF}')" = "openSUSE" ]
then
  sed 's/, cyrus-sasl-lib//' "${srcdir}/${specpath}" >"/tmp/${name}.spec"
  specpath="/tmp/${name}.spec"
fi

"${GFDOCKER_SCRIPT_PATH}/rpm-build.sh" -O -s "${specpath}" ${name}
