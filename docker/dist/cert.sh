#!/bin/sh
set -xeu
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
HOST=$(hostname)
HOSTS=$(grep -v $HOST ~/.nodelist)
mkdir -p ~/local/certs
cp -p $GSDIR/certificates/$HASH.* \
	$GSDIR/certificates/*.$HASH ~/local/certs

for h in $HOSTS
do
	ssh $h sudo mkdir -p $GSDIR/certificates
	ssh $h sudo cp local/certs/* $GSDIR/certificates
	ssh $h sudo grid-default-ca -ca $HASH > /dev/null
done
rm -rf ~/local/certs

# host cert
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

for h in $HOSTS
do
	ssh $h test -f $GSDIR/hostcert.pem && continue

	mkdir -p ~/local/$h
	ssh $h "yes | sudo grid-cert-request -host $h" > /dev/null 2>&1
	ssh $h cp $GSDIR/hostcert_request.pem local/$h
	sh ./cert-sign.sh ~/local/$h/hostcert_request.pem \
		~/local/$h/hostcert.pem
	ssh $h sudo cp local/$h/hostcert.pem $GSDIR
	rm -rf ~/local/$h
done

for h in $HOSTS
do
	ssh $h test -f $GSDIR/$SERVICE/${SERVICE}cert.pem && continue

	mkdir -p ~/local/$h
	ssh $h "yes | sudo grid-cert-request -service $SERVICE -host $h" \
		> /dev/null 2>&1
	ssh $h cp $GSDIR/$SERVICE/${SERVICE}cert_request.pem local/$h
	sh ./cert-sign.sh ~/local/$h/${SERVICE}cert_request.pem \
		~/local/$h/${SERVICE}cert.pem
	ssh $h sudo cp local/$h/${SERVICE}cert.pem  $GSDIR/$SERVICE
	ssh $h sudo chown -R _gfarmfs:_gfarmfs $GSDIR/$SERVICE
	rm -rf ~/local/$h
done
status=0
