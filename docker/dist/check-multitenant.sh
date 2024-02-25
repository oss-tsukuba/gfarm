#!/bin/sh
# this script should be executed after check-oauth.sh

${DEBUG:=false} && set -x
set -eu

gfstatus -S | egrep -v not | grep sasl > /dev/null || exit 0
jwt-parse > /dev/null || exit 0

REGRESS=
while [ $# -gt 0 ]
do
        case $1 in
        pkg|min)
            REGRESS=false ;;
        regress)
            [ X$REGRESS = X ] && REGRESS=true ;;
        *) exit 1 ;;
        esac
        shift
done
[ X$REGRESS = X ] && REGRESS=false

echo "*** multitenant ***"
: ${USER:=$(id -un)}
gfuser -A $USER SASL ""
sh ./edconf.sh sharedsecret > /dev/null
sh ./init-tenant.sh
sh ./create-tenant.sh A user1
sh ./edconf.sh oauth2 > /dev/null
gfwhoami
sh ./check.sh
if $REGRESS; then
	for h in c6 c7 c8; do
		ssh $h gfuser -A $USER SASL \"\"
		ssh $h sh $PWD/edconf.sh sharedsecret > /dev/null
		ssh $h sh $PWD/init-tenant.sh
		ssh $h sh $PWD/create-tenant.sh A user1
		ssh $h sh $PWD/edconf.sh oauth2 > /dev/null
		ssh $h gfwhoami
		ssh $h sh $PWD/check.sh
	done
	sh ./regress.sh
fi

gfuser -A gfarmadm SASL ""
sh ./edconf.sh sharedsecret > /dev/null
if $REGRESS; then
	for h in c6 c7 c8; do
		ssh $h gfuser -A gfarmadm SASL \"\"
		ssh $h sh $PWD/edconf.sh sharedsecret > /dev/null
	done
fi
