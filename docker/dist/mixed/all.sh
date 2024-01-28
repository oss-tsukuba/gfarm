#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo All set || echo NG: $PROG; exit $status' 0 1 2 15

REGRESS=false

# sanity
DISTDIR=$PWD/..
[ -f $DISTDIR/install.sh ]
[ -f $DISTDIR/config.sh ]

# for GitHub Actions:
# change the owner of /home/runner/local from root to runner
sudo chown `id -un` ~/local

# set up .nodelist
sh $DISTDIR/setup.sh

# install Gfarm
(cd ~/gfarm && sh $DISTDIR/install.sh single)
gfarm-prun -v "(cd ~/gfarm && sh $DISTDIR/install.sh single)"
gfarm-pcp -p ~/.nodelist .
[ -f ~/.gfarm2rc ] && gfarm-prun -a -p "mv ~/.gfarm2rc ~/.gfarm2rc.bak
	> /dev/null 2>&1"

# install Gfarm2fs
PKG=gfarm2fs; export PKG
[ -d ~/gfarm/$PKG ] || git clone https://github.com/oss-tsukuba/$PKG.git
gfarm-prun -a -v "(export PKG=$PKG; cd ~/gfarm/$PKG && sh $DISTDIR/install.sh single)"

# install jwt-logon
PKG=jwt-logon; export PKG
[ -d ~/gfarm/$PKG ] || git clone https://github.com/oss-tsukuba/$PKG.git
gfarm-prun -a -p "(cd gfarm/$PKG && sudo make PREFIX=/usr/local install
	 > /dev/null)"

# install jwt-agent
PKG=jwt-agent; export PKG
[ -d ~/gfarm/$PKG ] || git clone https://github.com/oss-tsukuba/$PKG.git
gfarm-prun -a -v "(cd ~/gfarm/$PKG && make clean > /dev/null && make > /dev/null
	&& sudo make PREFIX=/usr/local install > /dev/null)"

# install cyrus-sasl-xoauth2-idp
PKG=cyrus-sasl-xoauth2-idp; export PKG
sasl_libdir=$(pkg-config --variable=libdir libsasl2)
[ -d ~/gfarm/$PKG ] || git clone https://github.com/oss-tsukuba/$PKG.git
gfarm-prun -a -v "(cd ~/gfarm/$PKG && ./autogen.sh &&
	 ./configure --libdir=\$(pkg-config --variable=libdir libsasl2) &&
	 make > /dev/null && sudo make install > /dev/null)"

cat <<EOF | sudo tee $sasl_libdir/sasl2/gfarm.conf > /dev/null
log_level: 7
mech_list: XOAUTH2 ANONYMOUS
xoauth2_scope: hpci
xoauth2_aud: hpci
xoauth2_user_claim: hpci.id
EOF
cat <<EOF | sudo tee $sasl_libdir/sasl2/gfarm-client.conf > /dev/null
xoauth2_user_claim: hpci.id
EOF

cp $sasl_libdir/sasl2/gfarm*.conf ~/local
gfarm-prun -p sudo cp local/gfarm*.conf \$\(pkg-config --variable=libdir libsasl2\)/sasl2
rm ~/local/gfarm*.conf

# create empty sasldb2 database,
# because the gdbm backend of Cyrus SAL (e.g. on RHEL9) needs this
# although the berkeley DB backend does NOT
gfarm-prun -p -a "sudo saslpasswd2 -d -u NOT-EXIST NOT-EXIST"

# XXX - SASL XOAUTH2 fails in gfsd on ubuntu due to the error
# "unable to open Berkeley db /etc/sasldb2: Permission denied"
gfarm-prun -p -a \
	"sudo chown _gfarmfs /etc/sasldb2 /etc/sasl2/sasldb2 > /dev/null 2>&1"

# set up certificates
sh $DISTDIR/key.sh
sh $DISTDIR/userkey.sh
sh $DISTDIR/cert.sh
sh $DISTDIR/usercert.sh
sh $DISTDIR/tlscert.sh

# set up Gfarm-1 with 5 nodes
echo c1 c2 c3 c4 c5 | sh $DISTDIR/config.sh - &

# set up Gfarm-2 to Gfarm-4 with 1 node
for h in c6 c7 c8; do
	echo $h | ssh $h sh $DISTDIR/config.sh - &
done
wait

# Check installation
AUTH=
for a in $(gfstatus -S | grep 'client auth' | grep -v not | awk '{ print $3 }')
do
	[ $a = gsi ] && AUTH="$AUTH gsi gsi_auth"
	[ $a = tls ] && AUTH="$AUTH tls_sharedsecret tls_client_certificate"
	[ $a = sasl ] && AUTH="$AUTH anonymous"
done
AUTH="$AUTH sharedsecret"
for a in $AUTH
do
	echo "*** $a ***"
	sh $DISTDIR/edconf.sh $a > /dev/null
	sh $DISTDIR/check.sh
	for h in c6 c7 c8; do
		ssh $h sh $DISTDIR/edconf.sh $a > /dev/null
		ssh $h sh $DISTDIR/check.sh
	done
	$REGRESS && sh $DISTDIR/regress.sh
done

status=0
