#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; rm -f ~/local/grid-mapfile; exit $status' \
	0 1 2 15

PASS=globus
DIGEST=sha256
: ${USER:=$(id -un)}

[ -f ~/.globus/usercert.pem ] && {
	status=0
	exit 0
}

grid-cert-request -cn $USER -nopw
echo $PASS | sudo grid-ca-sign -in ~/.globus/usercert_request.pem \
	 -out ~/.globus/usercert.pem -passin stdin -md $DIGEST
grid-proxy-init
SUB=$(grid-proxy-info -issuer)
echo \"$SUB\" $USER | sudo tee -a /etc/grid-security/grid-mapfile > /dev/null
cp /etc/grid-security/grid-mapfile ~/local

gfarm-pcp -p ~/.globus .
gfarm-prun -p sudo cp local/grid-mapfile /etc/grid-security

status=0
echo Done
