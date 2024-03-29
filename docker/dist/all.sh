#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo All set || echo NG: $PROG; exit $status' 0 1 2 15

build_pkg=false
gfarm_config=all
install_option=
REGRESS=false
REGRESS_FULL=false

while [ $# -gt 0 ]
do
	case $1 in
	pkg) build_pkg=true
	     REGRESS=false ;;
	min) gfarm_config=min
	     install_option=single
	     REGRESS=false ;;
	regress|regress_full)
	     $build_pkg || {
	     gfarm_config=all
	     install_option=
	     REGRESS=true
	     [ $1 = "regress_full" ] && REGRESS_FULL=true
	     } ;;
	*) exit 1 ;;
	esac
	shift
done

# sanity
[ -f ./install.sh ]
[ -f ./config.sh ]
DISTDIR=$PWD

# for GitHub Actions:
# change the owner of /home/runner/local from root to runner
sudo chown `id -un` ~/local

# set up .nodelist
sh ./setup.sh

# install Gfarm
if $build_pkg; then
	(cd && sh $DISTDIR/mkrpm.sh)
	sh ./install-rpm.sh
else
	(cd ~/gfarm && sh $DISTDIR/install.sh $install_option)
fi
gfarm-pcp -p ~/.nodelist .
[ -f ~/.gfarm2rc ] && gfarm-prun -a -p "mv ~/.gfarm2rc ~/.gfarm2rc.bak
	> /dev/null 2>&1"

# install Gfarm2fs
PKG=gfarm2fs; export PKG
[ -d ~/gfarm/$PKG ] || git clone https://github.com/oss-tsukuba/$PKG.git
if $build_pkg; then
	(cd ~/gfarm && sh $DISTDIR/mkrpm.sh)
	sh ./install-rpm.sh
else
	(cd ~/gfarm/$PKG && sh $DISTDIR/install.sh $install_option)
fi

# install jwt-logon
PKG=jwt-logon; export PKG
[ -d ~/gfarm/$PKG ] || git clone https://github.com/oss-tsukuba/$PKG.git
if $build_pkg; then
	(cd ~/gfarm && sh $DISTDIR/mkrpm.sh)
	sh ./install-rpm.sh
else
	(cd ~/gfarm/$PKG && sudo make PREFIX=/usr/local install > /dev/null &&
	 gfarm-prun -p "(cd gfarm/$PKG && sudo make PREFIX=/usr/local install
		 > /dev/null)")
fi

# install jwt-agent
PKG=jwt-agent; export PKG
[ -d ~/gfarm/$PKG ] || git clone https://github.com/oss-tsukuba/$PKG.git
if $build_pkg; then
	(cd ~/gfarm && sh $DISTDIR/mkrpm.sh)
	sh ./install-rpm.sh
else
	(cd ~/gfarm/$PKG && make clean > /dev/null && make > /dev/null &&
	 sudo make PREFIX=/usr/local install > /dev/null &&
	 gfarm-prun -p "(cd gfarm/$PKG && sudo make PREFIX=/usr/local install
		 > /dev/null)")
fi

# install cyrus-sasl-xoauth2-idp
PKG=cyrus-sasl-xoauth2-idp; export PKG
sasl_libdir=$(pkg-config --variable=libdir libsasl2)
[ -d ~/gfarm/$PKG ] || git clone https://github.com/oss-tsukuba/$PKG.git
if $build_pkg; then
	(cd ~/gfarm && sh $DISTDIR/mkrpm.sh)
	sh ./install-rpm.sh
else
	(cd ~/gfarm/$PKG && ./autogen.sh &&
	 ./configure --libdir=$sasl_libdir &&
	 make > /dev/null && sudo make install > /dev/null &&
	 gfarm-prun -p "(cd gfarm/$PKG && sudo make install > /dev/null)")
fi

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
gfarm-prun -p sudo cp local/gfarm*.conf $sasl_libdir/sasl2
rm ~/local/gfarm*.conf

# set up certificates
sh ./key.sh
sh ./userkey.sh
sh ./cert.sh
sh ./usercert.sh
sh ./tlscert.sh

if [ $gfarm_config = all ]; then
	# set up Gfarm-1 with 5 nodes
	echo c1 c2 c3 c4 c5 | sh ./config.sh - &

	# set up Gfarm-2 to Gfarm-4 with 1 node
	for h in c6 c7 c8; do
		echo $h | ssh $h sh $PWD/config.sh - &
	done
	wait
else
	echo c1 | sh ./config.sh -
fi

# Check installation
AUTH=
for a in $(gfstatus -S | grep 'client auth' | grep -v not | awk '{ print $3 }')
do
	[ $a = gsi ] && AUTH="$AUTH gsi gsi_auth"
	[ $a = tls ] && AUTH="$AUTH tls_sharedsecret tls_client_certificate"
	[ $a = sasl ] && AUTH="$AUTH anonymous anonymous_auth"
done
AUTH="$AUTH sharedsecret"
for a in $AUTH
do
	echo "*** $a ***"
	sh ./edconf.sh $a > /dev/null
	sh ./check.sh
	if [ $gfarm_config = all ]; then
		for h in c6 c7 c8; do
			ssh $h sh $PWD/edconf.sh $a > /dev/null
			ssh $h sh $PWD/check.sh
		done
	fi
	case $a in
	gsi*|\
	tls_sharedsecret|anonymous_auth)
		$REGRESS_FULL || continue ;;
	esac
	$build_pkg && continue
	$REGRESS && sh ./regress.sh
	$REGRESS && sh ./regress-xattr.sh
done

status=0
