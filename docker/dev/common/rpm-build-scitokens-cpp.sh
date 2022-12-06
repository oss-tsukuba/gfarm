#!/bin/sh

set -eux

GFDOCKER_SCRIPT_PATH="`dirname $0`"
name=scitokens-cpp

"${GFDOCKER_SCRIPT_PATH}/rpm-build.sh" -O \
	-F	"/usr/include/sqlite3.h
		 /usr/include/curl/curl.h
		 /usr/include/uuid/uuid.h" \
	-T "cmake c++" \
	-s "rpm/${name}.spec" \
	${name}
