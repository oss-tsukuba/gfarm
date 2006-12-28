#!/bin/sh

. ./regress.conf

if [ "${REGRESS_GFARMFS_FUSE+set}" != "set" ] ||
   [ ! -d "$REGRESS_GFARMFS_FUSE" ]; then
	exit $exit_unsupported
fi

cd "$REGRESS_GFARMFS_FUSE/test" || exit $exit_unsupported

if make check; then
	exit_code=$exit_pass
fi
exit $exit_code
