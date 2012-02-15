#!/bin/sh

Config_sharedsecret="${HOME}/.gfperf.sharedsecret.conf"
Config_gsi_auth="${HOME}/.gfperf.gsi_auth.conf"
Config_gsi="${HOME}/.gfperf.gsi.conf"

if [ -f ${HOME}/.gfperfrc ]; then
    . ${HOME}/.gfperfrc
fi

if [ $# -lt 2 ]; then
    echo "usage: $0 <type> <command>" >&2
    echo "<type> must be sharedsecret or gsi_auth or gsi" >&2
    exit 1
fi

case $1 in
  sharedsecret) config=${Config_sharedsecret} ;;
  gsi_auth) config=${Config_gsi_auth} ;;
  gsi) config=${Config_gsi} ;;
  *) echo "type must be sharedsecret or gsi_auth or gsi." >&2
     exit 1 ;;
esac

if [ ! -f $Config_sharedsecret ]; then
    echo "auth enable sharedsecret *" > $Config_sharedsecret
    echo "auth disable gsi_auth *" >> $Config_sharedsecret
    echo "auth disable gsi *" >> $Config_sharedsecret
fi

if [ ! -f $Config_gsi_auth ]; then
    echo "auth disable sharedsecret *" > $Config_gsi_auth
    echo "auth enable gsi_auth *" >> $Config_gsi_auth
    echo "auth disable gsi *" >> $Config_gsi_auth
fi

if [ ! -f $Config_gsi ]; then
    echo "auth disable sharedsecret *" > $Config_gsi
    echo "auth disable gsi_auth *" >> $Config_gsi
    echo "auth enable gsi *" >> $Config_gsi
fi

export GFARM_CONFIG_FILE=$config
shift
exec $*
