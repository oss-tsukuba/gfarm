#!/bin/sh
set -eu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

# Some distributions (openSUSE) create /var/lib/globus/simple_ca/ when the
# globus-simple-ca package is installed.  That does not mean that the
# dist test CA below has been created yet.
[ -f /var/lib/globus/simple_ca/.gfarm-test-ca ] && {
	status=0
	exit 0
}

# install and create simple ca
GSDIR=/etc/grid-security
sudo grid-ca-create -noint -subject "cn=CA, ou=GfarmTest, o=Grid" -nobuild
sudo touch /var/lib/globus/simple_ca/.gfarm-test-ca
rm -f openssl_req.log
CA_HASH=
for f in $GSDIR/certificates/*.0
do
	SUBJECT=$(openssl x509 -in "$f" -noout -subject 2>/dev/null || :)
	if echo "$SUBJECT" | grep -Eq 'CN[[:space:]]*=[[:space:]]*CA' &&
	   echo "$SUBJECT" | \
		    grep -Eq 'OU[[:space:]]*=[[:space:]]*GfarmTest'; then
		B=$(basename "$f")
		CA_HASH=${B%.0}
		[ -f "$GSDIR/certificates/grid-security.conf.$CA_HASH" ] \
		    && break
	fi
done
[ -n "$CA_HASH" ] || {
	echo "cannot find the GfarmTest CA certificate" >&2
	exit 1
}
HASH=$CA_HASH
sudo grid-default-ca -ca $HASH > /dev/null

# copy CA cert
(cd $GSDIR/certificates && tar cf ~/local/certs.tar $HASH.* *.$HASH)

gfarm-prun -p "sudo mkdir -p $GSDIR/certificates &&
	sudo tar xf local/certs.tar -C $GSDIR/certificates &&
	sudo grid-default-ca -ca $HASH > /dev/null"
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
