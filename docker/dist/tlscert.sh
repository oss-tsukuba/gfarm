#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

GSDIR=/etc/grid-security
TLSDIR=/etc/pki/tls

[ -h $TLSDIR/certs/gfarm ] && {
	status=0
	exit 0
}

gfarm-prun -a -p sudo mkdir -p $TLSDIR/{certs,private}
gfarm-prun -a -p sudo ln -s $GSDIR/certificates $TLSDIR/certs/gfarm

gfarm-prun -a -p sudo ln -s $GSDIR/hostcert.pem $TLSDIR/certs/gfmd.crt
gfarm-prun -a -p sudo ln -s $GSDIR/hostkey.pem $TLSDIR/private/gfmd.key
gfarm-prun -a -p sudo ln -s $GSDIR/gfsd/gfsdcert.pem $TLSDIR/certs/gfsd.crt
gfarm-prun -a -p sudo ln -s $GSDIR/gfsd/gfsdkey.pem $TLSDIR/private/gfsd.key

gfarm-prun -a -p "
if grep debian /etc/os-release > /dev/null; then
	sudo ln -s /minica /usr/share/ca-certificates/ &&
	echo minica/minica.crt | sudo tee -a /etc/ca-certificates.conf
		> /dev/null &&
	sudo update-ca-certificates;
elif grep rhel /etc/os-release > /dev/null; then
	sudo cp /minica/minica.crt /usr/share/pki/ca-trust-source/anchors/ &&
	sudo update-ca-trust;
fi"

status=0
