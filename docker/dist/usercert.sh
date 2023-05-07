#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; \
	rm -f ~/local/grid-mapfile; exit $status' 0 1 2 15

: ${USER:=$(id -un)}

[ -f ~/.globus/usercert.pem ] && {
	status=0
	exit 0
}

grid-cert-request -cn $USER -nopw > /dev/null 2>&1
sh ./cert-sign.sh ~/.globus/usercert_request.pem ~/.globus/usercert.pem
grid-proxy-init -q
SUB=$(grid-proxy-info -issuer)
echo \"$SUB\" $USER | sudo tee -a /etc/grid-security/grid-mapfile > /dev/null
cp /etc/grid-security/grid-mapfile ~/local

gfarm-pcp -p ~/.globus .
gfarm-prun -p sudo cp local/grid-mapfile /etc/grid-security

status=0
