#!/bin/sh
set -xeu

gfstatus -S | egrep -v not | grep sasl > /dev/null || exit 0

REGRESS=false
REGRESS_GFARM2FS=false
while [ $# -gt 0 ]
do
	case $1 in
	pkg|min)
	    REGRESS=false ;;
	regress|regress_full)
	    REGRESS=true ;;
	regress_gfarm2fs|regress_gfarm2fs_full)
	    REGRESS_GFARM2FS=true ;;
	*) exit 1 ;;
	esac
	shift
done
SASL_USER=user1
SERVER=https://jwt-server

run_jwt_agent()
{
	pass=$1
	host=
	[ $# -gt 1 ] && host=$2

	 if [ X$host = X ]; then
		echo $pass | jwt-agent -s $SERVER -l $SASL_USER
	 else
		echo $pass | ssh $host jwt-agent -s $SERVER -l $SASL_USER
	 fi
}

echo "*** oauth2 ***"
: ${USER:=$(id -un)}
PASSF=~/local/.jwt-pass
PASS=
jwt-parse > /dev/null || {
	PASS=$(sh ./init-jwt.sh)
	[ X$PASS = X ] && PASS=$(cat $PASSF) || echo $PASS > $PASSF
	[ X$PASS = X ] || run_jwt_agent $PASS
}
sh ./edconf.sh sharedsecret > /dev/null
gfuser -A $USER SASL $SASL_USER

sh ./edconf.sh oauth2 > /dev/null
sh ./check.sh
if $REGRESS || $REGRESS_GFARM2FS; then
	[ X$PASS = X ] && PASS=$(cat $PASSF)
	[ X$PASS = X ] || run_jwt_agent $PASS c2
	for h in c6 c7 c8; do
		[ X$PASS = X ] || run_jwt_agent $PASS $h
		ssh $h gfuser -A gfarmadm SASL \"\"
		ssh $h gfuser -A gfarmadm+A SASL \"\" || :
		ssh $h gfuser -A $USER SASL $SASL_USER
		ssh $h sh $PWD/edconf.sh oauth2 > /dev/null
		ssh $h sh $PWD/check.sh
	done
	$REGRESS && sh ./regress.sh
	$REGRESS_GFARM2FS && ~/gfarm/gfarm2fs/regress/regress.sh
fi

echo "$0: Done"
