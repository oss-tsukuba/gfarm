#!/bin/sh
set -eu

GFCONF=~/.gfarm2rc
for d in /etc /usr/local/etc
do
	[ -f $d/gfarm2.conf ] && break
done
[ -f $d/gfarm2.conf ] && SYSCONF=$d/gfarm2.conf || exit 1

DEBUG=false
AUTH=
SASL=
while [ $# -gt 0 ]
do
	case $1 in
	debug)	DEBUG=true ;;
	sharedsecret|gsi|gsi_auth|\
	tls_sharedsecret|tls_client_certificate|\
	sasl|sasl_auth)
		AUTH="$AUTH $1" ;;
	xoauth2|oauth2)
		jwt-parse > /dev/null
		AUTH="$AUTH sasl"
		SASL="sasl_mechanisms XOAUTH2" ;;
	anonymous)
		gfuser anonymous > /dev/null 2>&1 || {
			gfuser -c anonymous anon / ""
			gfgroup -m -a gfarmadm anonymous
		}
		AUTH="$AUTH sasl"
		SASL="sasl_mechanisms ANONYMOUS\nsasl_user dummy" ;;
	*) exit 1 ;;
	esac
	shift
done

[ -f $GFCONF ] || touch $GFCONF

# update log_level and log_auth_verbose
cp -p $GFCONF $GFCONF.bak
sed '/^log_level/d
     /^log_auth_verbose/d' $GFCONF.bak > $GFCONF

if $DEBUG; then
	cat <<EOF >> $GFCONF
log_level debug
log_auth_verbose enable
EOF
fi

# update auth
[ X"$AUTH" = X ] || {
	cp -p $GFCONF $GFCONF.bak
	sed '/^auth/d' $GFCONF.bak > $GFCONF
	for a in $AUTH; do
		echo "auth enable $a *" >> $GFCONF
	done
	awk '/^auth/ {print "auth disable", $3, $4}' $SYSCONF >> $GFCONF
}
[ X"$SASL" = X ] || {
	cp -p $GFCONF $GFCONF.bak
	sed '/^sasl/d' $GFCONF.bak > $GFCONF
	/usr/bin/echo -e $SASL >> $GFCONF
}

echo [~/.gfarm2rc]
cat $GFCONF

gfhost | gfarm-pcp -h - -p $GFCONF .

echo Done
