#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

GSDIR=/etc/grid-security
TLSDIR=/etc/pki/tls

gfarm-prun -a -p sudo mkdir -p $TLSDIR/{certs,private}
gfarm-prun -a -p sudo ln -s $GSDIR/certificates $TLSDIR/certs/gfarm

gfarm-prun -a -p sudo ln -s $GSDIR/hostcert.pem $TLSDIR/certs/gfmd.crt
gfarm-prun -a -p sudo ln -s $GSDIR/hostkey.pem $TLSDIR/private/gfmd.key
gfarm-prun -a -p sudo ln -s $GSDIR/gfsd/gfsdcert.pem $TLSDIR/certs/gfsd.crt
gfarm-prun -a -p sudo ln -s $GSDIR/gfsd/gfsdkey.pem $TLSDIR/private/gfsd.key

status=0
