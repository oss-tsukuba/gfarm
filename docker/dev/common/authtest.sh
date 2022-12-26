#!/bin/sh

: $GFDOCKER_PRJ_NAME
: $GFDOCKER_NUM_GFMDS
: $GFDOCKER_NUM_GFSDS

set -eux

tls_available=false
if gfstatus 2>/dev/null | grep '^client auth tls ' | grep ': available'; then
  tls_available=true
fi

krb_available=false
if gfstatus 2>/dev/null | grep '^client auth kerberos' | grep ': available'
then
  krb_available=true
fi

sasl_available=false
if gfstatus 2>/dev/null | grep '^client auth sasl ' | grep ': available'; then
  sasl_available=true
fi

set +x

test_auth() {
  echo "***** $authtype *****"
  gfmdhost -l
  gfhost -lv
  num_gfmdhost=$(gfmdhost | wc -l)
  num_gfsched=$(gfsched | wc -l)
  [ $num_gfmdhost -eq $GFDOCKER_NUM_GFMDS ] || exit 1
  [ $num_gfsched -eq $GFDOCKER_NUM_GFSDS ] || exit 1
}

auth_trial=
if $tls_available; then
  auth_trial="${auth_trial} tls_sharedsecret tls_client_certificate"
fi
if $sasl_available; then
  auth_trial="${auth_trial} sasl_auth.plain sasl.plain"
fi
# FIXME: do not try kerberos for now, because "kadmin ktadd" sometimes fails
# to generate $HOME/.keytab.  the reason of the failure is not known yet.

for authtype in sharedsecret gsi_auth gsi $auth_trial; do
  case ${authtype} in
  sasl*) authconfig \
	    --client-server "${authtype}" \
	    --server-server tls_client_certificate;;
  *)	 authconfig "${authtype}";;
  esac
  test_auth
done
authconfig all

# LOG=/var/log/messages
# SYSLOG=/var/log/syslog
# if [ -f $SYSLOG ]; then
#    LOG=$SYSLOG
# fi
# sudo tail -1 $LOG

### check autofs

SUPPORT_AUTOFS=true
ls /gfarm/ROOT || SUPPORT_AUTOFS=false
ls /gfarm/user1 || SUPPORT_AUTOFS=false

echo -n "GFDOCKER_PRJ_NAME=$GFDOCKER_PRJ_NAME: autofs (/gfarm/*) "
if ${SUPPORT_AUTOFS}; then
    echo "works."
else
    echo "does not work."
fi

date
echo "GFDOCKER_PRJ_NAME=$GFDOCKER_PRJ_NAME: setup is now complete."
