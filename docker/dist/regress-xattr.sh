#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

cd ~/gfarm/regress
sh gftool/gfxattr/gfxattr-xml-enabled.sh || sh gftool/gfxattr/gfxattr-fini.sh

cd gftool/gfxattr
sh ./gffindxmlattr-large-test.sh

status=0
