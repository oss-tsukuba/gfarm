PATH=$PATH:/sbin:/usr/sbin; export PATH

krb_server="${GFDOCKER_HOSTNAME_PREFIX_GFMD}1${GFDOCKER_HOSTNAME_SUFFIX}"
krb_realm=`echo "${GFDOCKER_HOSTNAME_SUFFIX}" | sed 's/^\.//'`
if [ -d /etc/krb5kdc ]; then
    # Debian/Ubuntu
    kadm_acl=/etc/krb5kdc/kadm5.acl
    kdc_conf=/etc/krb5kdc/kdc.conf

    krb5kdc_service=krb5-kdc
    kadmin_service=krb5-admin-server

elif [ -d /var/lib/kerberos/krb5kdc ]; then
    # openSUSE
    kadm_acl=/var/lib/kerberos/krb5kdc/kadm5.acl
    kdc_conf=/var/lib/kerberos/krb5kdc/kdc.conf

    krb5kdc_service=krb5kdc
    kadmin_service=kadmind

else
    # RHEL
    kadm_acl=/var/kerberos/krb5kdc/kadm5.acl
    kdc_conf=/var/kerberos/krb5kdc/kdc.conf

    krb5kdc_service=krb5kdc
    kadmin_service=kadmin
fi
krb_conf=/etc/krb5.conf
krb_keytab=/etc/krb5.keytab
krb_gfsd_keytab=/etc/gfsd.keytab
krb_admin_user=admin_user1
krb_master_password=KRB_MASTER_PASSWORD
krb_admin_password=KRB_ADMIN_PASSWORD
krb_user_password=PASSWORD

rewrite_krb_conf() {
    ${SUDO} sh -c "
        [ -f '${krb_conf}.bak' ] || cp -p '${krb_conf}' '${krb_conf}.bak'"

    # replace:
    #   default_realm = <something>
    # with:
    #   default_realm = ${krb_realm}
    #
    # add the following lines to the [realms] section
    #	${krb_realm} = {
    #	    kdc = ${krb_server}
    #	    admin_server = ${krb_server}
    #	}
    ${SUDO} awk '
        /^\[libdefaults\]/ { section = "libdefaults"; print; next }
        /^\[realms\]/ { section = "realms"; print; next }
        /^\[/ { section = ""; print; next }
        section == "libdefaults" && /^[ \t]*#*[ \t]*default_realm[ \t]*=/ {
            print "    default_realm = '"${krb_realm}"'"
            next
        }
        section == "realms" && /^[ \t]*$/ {
            print "'"${krb_realm}"' = {"
            print "    kdc = '"${krb_server}"'"
            print "    admin_server = '"${krb_server}"'"
            print "}"
            print; next
        }
        { print; next }
    ' "${krb_conf}.bak" |
        sudo sh -c "cat >'${krb_conf}'"
}
