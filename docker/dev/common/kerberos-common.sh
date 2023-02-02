PATH=$PATH:/sbin:/usr/sbin; export PATH

krb_server="${GFDOCKER_HOSTNAME_PREFIX_GFMD}1${GFDOCKER_HOSTNAME_SUFFIX}"
krb_realm=`echo "${GFDOCKER_HOSTNAME_SUFFIX}" | sed 's/^\.//'`
kadm_acl=/var/kerberos/krb5kdc/kadm5.acl
kdc_conf=/var/kerberos/krb5kdc/kdc.conf
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

    # add the following line to the [libdefaults] section
    #	default_realm = ${krb_realm}
    # add the following lines to the [realms] section
    #	${krb_realm} = {
    #	    kdc = ${krb_server}
    #	    admin_server = ${krb_server}
    #	}
    ${SUDO} awk '
        /^\[libdefaults\]/ { section = "libdefaults"; print; next }
        /^\[realms\]/ { section = "realms"; print; next }
        /^\[/ { section = ""; print; next }
        section == "libdefaults" && /^[ 	]*$/ {
            print "    default_realm = '"${krb_realm}"'"
            print; next
        }
        section == "realms" && /^[ 	]*$/ {
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
