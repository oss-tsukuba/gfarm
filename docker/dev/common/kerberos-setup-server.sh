#!/bin/bash

set -eux

BASEDIR=$(dirname $(realpath $0))
FUNCTIONS=${BASEDIR}/functions.sh
. ${FUNCTIONS}
. ${BASEDIR}/kerberos-common.sh

replace_kadm_acl() {
    ${SUDO} sh -c "
        [ -f '${kadm_acl}.bak' ] || cp -p '${kadm_acl}' '${kadm_acl}.bak'"

    # replace the following
    #	*/admin@EXAMPLE.COM	*
    # to
    #	*/admin@${krb_realm}	*
    echo "*/admin@${krb_realm}	*" | sudo sh -c "cat >'${kadm_acl}'"
}

rewrite_kdc_conf() {
    ${SUDO} sh -c "
        [ -f '${kdc_conf}.bak' ] || cp -p '${kdc_conf}' '${kdc_conf}.bak'"

    # rewrite the following
    #	[realms]
    #	EXAMPLE.COM = {
    # to
    #	[realms]
    #	${krb_realm} = {
    ${SUDO} awk '
        /^\[realms\]/ { section = "realms"; print; next }
        /^\[/ { section = ""; print; next }
        section == "realms" &&
            /^[ 	]*[0-9A-Za-z][-.0-9A-Za-z]*[ 	]*=[ 	]{/ {

            sub(/^[ 	]*[0-9A-Za-z][-.0-9A-Za-z]*[ 	]*=[ 	]{/, "'"${krb_realm} = {"'")
            print; next
        }
        { print; next }
    ' "${kdc_conf}.bak" |
        sudo sh -c "cat >'${kdc_conf}'"
}

replace_kadm_acl
rewrite_kdc_conf
rewrite_krb_conf

${SUDO} kdb5_util -P "${krb_master_password}" create -r "${krb_realm}" -s
${SUDO} systemctl enable krb5kdc
${SUDO} systemctl enable kadmin
${SUDO} systemctl restart krb5kdc
${SUDO} systemctl restart kadmin

${SUDO} kadmin.local add_principal -pw "${krb_admin_password}" \
    "${krb_admin_user}/admin"

i=0
for t in $(seq 1 "$GFDOCKER_NUM_TENANTS"); do
  for u in $(seq 1 "$GFDOCKER_NUM_USERS"); do
    i=$((i + 1))
    user="${GFDOCKER_USERNAME_PREFIX}${i}"
    ${SUDO} kadmin.local add_principal -pw "${krb_user_password}" "${user}"
  done
done

${SUDO} kadmin.local -q list_principals
