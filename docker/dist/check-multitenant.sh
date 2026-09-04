#!/bin/sh
# this script should be executed after check-oauth.sh

${DEBUG:=false} && set -x
set -eu

gfstatus -S | egrep -v not | grep sasl > /dev/null || exit 0
jwt-parse > /dev/null || exit 0

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
echo "*** multitenant ***"
: ${USER:=$(id -un)}
sh ./edconf.sh sharedsecret > /dev/null
gfuser -A $USER SASL ""

sh ./init-tenant.sh
sh ./create-tenant.sh A user1
sh ./edconf.sh oauth2 > /dev/null
gfwhoami
sh ./check.sh
if $REGRESS || $REGRESS_GFARM2FS; then
	for h in c6 c7 c8; do
		ssh $h sh $PWD/edconf.sh sharedsecret > /dev/null
		ssh $h gfuser -A $USER SASL \"\" || :
		ssh $h gfuser -A gfarmadm SASL \"\" || :
		ssh $h gfuser -A gfarmadm+A SASL \"\" || :
		ssh $h sh $PWD/init-tenant.sh
		ssh $h sh $PWD/create-tenant.sh A user1
		ssh $h sh $PWD/edconf.sh oauth2 > /dev/null
		ssh $h gfwhoami
		ssh $h sh $PWD/check.sh
	done
	$REGRESS && sh ./regress.sh
	$REGRESS_GFARM2FS && ~/gfarm/gfarm2fs/regress/regress.sh
fi

# clean and restore
gfuser -A $USER SASL ""
gfuser -A gfarmadm SASL ""
sh ./edconf.sh sharedsecret > /dev/null
if $REGRESS || $REGRESS_GFARM2FS; then
	for h in c6 c7 c8; do
		ssh $h gfuser -A gfarmadm SASL \"\"
		ssh $h sh $PWD/edconf.sh sharedsecret > /dev/null
	done
fi

echo "$0: Done"
