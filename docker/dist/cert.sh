#!/bin/sh
set -eu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

[ -d /var/lib/globus/simple_ca/ ] && {
	status=0
	exit 0
}

# install and create simple ca
GSDIR=/etc/grid-security
sudo grid-ca-create -noint -subject "cn=CA, ou=GfarmTest, o=Grid" -nobuild
rm -f openssl_req.log
for f in $GSDIR/certificates/*.0
do
	HASH=$(openssl x509 -hash -in $f -noout)
	[ -f $GSDIR/certificates/grid-security.conf.$HASH ] && break
done
sudo grid-default-ca -ca $HASH > /dev/null

# copy CA cert
(cd $GSDIR/certificates && tar cf ~/local/certs.tar $HASH.* *.$HASH)

gfarm-prun -p sudo mkdir -p $GSDIR/certificates
gfarm-prun -p sudo tar xf local/certs.tar -C $GSDIR/certificates
gfarm-prun -p "sudo grid-default-ca -ca $HASH > /dev/null"
rm -f ~/local/certs.tar

# host cert
HOST=$(hostname)
[ -f $GSDIR/hostcert.pem ] || {
	yes | sudo grid-cert-request -host $HOST > /dev/null 2>&1
	sh ./cert-sign.sh $GSDIR/hostcert_request.pem $GSDIR/hostcert.pem
}

SERVICE=gfsd
[ -f $GSDIR/$SERVICE/${SERVICE}cert.pem ] || {
	yes | sudo grid-cert-request -service $SERVICE -host $HOST \
		> /dev/null 2>&1
	sh ./cert-sign.sh $GSDIR/$SERVICE/${SERVICE}cert_request.pem \
		$GSDIR/$SERVICE/${SERVICE}cert.pem
	sudo chown -R _gfarmfs:_gfarmfs $GSDIR/$SERVICE
}

gfarm-prun -p "[ -f $GSDIR/hostcert.pem ] || {
	h=\$(hostname) &&
	mkdir -p ~/local/\$h &&
	yes | sudo grid-cert-request -host \$h > /dev/null 2>&1 &&
	yes | sudo grid-cert-request -service $SERVICE -host \$h \
		> /dev/null 2>&1 &&
	cp $GSDIR/hostcert_request.pem \
		$GSDIR/$SERVICE/${SERVICE}cert_request.pem local/\$h; }"

for certreq in ~/local/*/*cert_request.pem
do
	cert=$(echo $certreq | sed 's/_request//')
	sh ./cert-sign.sh $certreq $cert
done

gfarm-prun -p "h=\$(hostname) &&
	sudo cp local/\$h/hostcert.pem $GSDIR &&
	sudo cp local/\$h/${SERVICE}cert.pem $GSDIR/$SERVICE &&
	sudo chown -R _gfarmfs:_gfarmfs $GSDIR/$SERVICE &&
	rm -rf ~/local/\$h"

status=0
