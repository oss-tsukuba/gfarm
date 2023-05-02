#!/bin/sh
set -xeu

PASS=globus
DIGEST=sha256
grid-cert-request -cn $USER -nopw
echo $PASS | sudo grid-ca-sign -in ~/.globus/usercert_request.pem \
	 -out ~/.globus/usercert.pem -passin stdin -md $DIGEST
grid-proxy-init
SUB=$(grid-proxy-info -issuer)
echo \"$SUB\" $USER | sudo tee -a /etc/grid-security/grid-mapfile > /dev/null
cp /etc/grid-security/grid-mapfile ~/local

for h in c2 c3 c4
do
	scp -pr ~/.globus $h:
done

for h in c2 c3 c4
do
	ssh $h sudo cp local/grid-mapfile /etc/grid-security
done

rm ~/local/grid-mapfile
