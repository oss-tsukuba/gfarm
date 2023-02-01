#!/bin/sh

set -eux

GFDOCKER_SCRIPT_PATH="`dirname $0`"
name=scitokens-cpp
srcdir=/work/gfarm/${name}
specpath="rpm/${name}.spec"

# OpenSUSE does not have "%cmake3"* macros of RPM spec file
if type lsb_release >/dev/null &&
   [ "$(lsb_release -i | awk '{print $NF}')" = "openSUSE" ]
then
  sed 's/^%cmake3/%cmake/' "${srcdir}/${specpath}" >"/tmp/${name}.spec"
  specpath="/tmp/${name}.spec"
fi

"${GFDOCKER_SCRIPT_PATH}/rpm-build.sh" -O \
	-F	"/usr/include/sqlite3.h
		 /usr/include/curl/curl.h
		 /usr/include/uuid/uuid.h" \
	-T "cmake c++" \
	-s "${specpath}" \
	${name}
