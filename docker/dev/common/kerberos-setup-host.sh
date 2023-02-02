#!/bin/bash

set -eux

BASEDIR=$(dirname $(realpath $0))
FUNCTIONS=${BASEDIR}/functions.sh
. ${FUNCTIONS}
. ${BASEDIR}/kerberos-common.sh

case $# in
1)
    host_type=$1;;
*)
    echo >&2 "$0: number of arguments $# is invalid"
    exit 2;;
esac
case ${host_type} in
gfmd|gfsd|client)
    :;;
*)
    echo >&2 "$0: invalid host type <${host_type}>"
    exit 2;;
esac

rewrite_krb_conf

hostname=`hostname`

case ${host_type} in
gfmd|gfsd)
    principal="host/${hostname}@${krb_realm}"
    ${SUDO} kadmin -p "${krb_admin_user}/admin" -w "${krb_admin_password}" \
        add_principal -randkey "${principal}"
    ${SUDO} kadmin -p "${krb_admin_user}/admin" -w "${krb_admin_password}" \
        ktadd "${principal}"
    ;;
esac

case ${host_type} in
gfsd)
    principal="gfsd/${hostname}@${krb_realm}"

    # the following 2 commands should be removed, when service cert works
    ${SUDO} kadmin -p "${krb_admin_user}/admin" -w "${krb_admin_password}" \
        add_principal -randkey "${principal}"
    ${SUDO} chown _gfarmfs "${krb_keytab}"

    ${SUDO} kadmin -p "${krb_admin_user}/admin" -w "${krb_admin_password}" \
        ktadd -k "${krb_gfsd_keytab}" "${principal}"
    ${SUDO} chown _gfarmfs "${krb_gfsd_keytab}"
    ;;
client)
    ## XXX currently this does not work.
    ## on CentOS 8:
    ##	kinit: Password incorrect while getting initial credentials
    #user_keytab="${HOME}/.keytab"
    #user="$(id -nu)"
    #principal="${user}@${krb_realm}"
    #${SUDO} kadmin -p "${krb_admin_user}/admin" -w "${krb_admin_password}" \
    #    ktadd -k "${user_keytab}" "${principal}"
    #${SUDO} chown "${user}" "${user_keytab}"
    ;;
esac

${SUDO} kadmin -p "${krb_admin_user}/admin" -w "${krb_admin_password}" \
    -q list_principals
