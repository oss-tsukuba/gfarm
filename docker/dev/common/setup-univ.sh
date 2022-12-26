#! /bin/sh

# Usage (in *-base-Dockerfile):
#
# ARG GFDOCKER_USERNAME_PREFIX
# ARG GFDOCKER_PRIMARY_USER
# ARG GFDOCKER_NUM_GFMDS
# ARG GFDOCKER_NUM_GFSDS
# ARG GFDOCKER_NUM_USERS
# ARG GFDOCKER_HOSTNAME_PREFIX_GFMD
# ARG GFDOCKER_HOSTNAME_PREFIX_GFSD
# ARG GFDOCKER_HOSTNAME_SUFFIX
# ARG GFDOCKER_USE_SAN_FOR_GFSD
# COPY --chown=1000:1000 . /work/gfarm
# RUN "/work/gfarm/docker/dev/common/setup-univ.sh"

set -eux

: $GFDOCKER_USERNAME_PREFIX
: $GFDOCKER_PRIMARY_USER
: $GFDOCKER_NUM_GFMDS
: $GFDOCKER_NUM_GFSDS
: $GFDOCKER_NUM_USERS
: $GFDOCKER_HOSTNAME_PREFIX_GFMD
: $GFDOCKER_HOSTNAME_PREFIX_GFSD
: $GFDOCKER_HOSTNAME_SUFFIX
: $GFDOCKER_USE_SAN_FOR_GFSD

MY_SHELL=/bin/bash
HOST_SHARE_DIR=/mnt
conf_include_dir=${HOST_SHARE_DIR}/conf
USERADD="useradd -m -s "$MY_SHELL" -U -K UID_MIN=1000 -K GID_MIN=1000"
SASL_DOMAIN=$(echo "$GFDOCKER_HOSTNAME_SUFFIX" | sed 's/^\.//')
SASL_PASSWORD_BASE=PASS-
ca_key_pass=PASSWORD
GRID_MAPFILE=/etc/grid-security/grid-mapfile
PRIMARY_HOME=/home/${GFDOCKER_PRIMARY_USER}
gfarm_src_path="/work/gfarm"
gfarm2fs_src_path="${gfarm_src_path}/gfarm2fs"
jwt_logon_src_path="${gfarm_src_path}/jwt-logon"
jwt_agent_src_path="${gfarm_src_path}/jwt-agent"
cyrus_sasl_xoauth2_idp_src_path="${gfarm_src_path}/cyrus-sasl-xoauth2-idp"
scitokens_cpp_src_path="${gfarm_src_path}/scitokens-cpp"

# pin starting UID for user1
for i in $(seq 1 "$GFDOCKER_NUM_USERS"); do
  $USERADD "${GFDOCKER_USERNAME_PREFIX}${i}";
done

ln -s ${gfarm_src_path} ${PRIMARY_HOME}/gfarm
ln -s ${gfarm2fs_src_path} ${PRIMARY_HOME}/gfarm2fs
# the following directories may not exist
ln -s ${jwt_logon_src_path} ${PRIMARY_HOME}/jwt-logon
ln -s ${jwt_agent_src_path} ${PRIMARY_HOME}/jwt-agent
ln -s ${cyrus_sasl_xoauth2_idp_src_path} ${PRIMARY_HOME}/cyrus-sasl-xoauth2-idp
ln -s ${scitokens_cpp_src_path} ${PRIMARY_HOME}/scitokens-cpp

# remove untracked files
(cd ${gfarm_src_path}; git clean -df) || true
(cd ${gfarm2fs_src_path}; git clean -df) || true
# the following directories may not exist
( if cd ${jwt_logon_src_path}; then git clean -df; fi ) || true
( if cd ${jwt_agent_src_path}; then git clean -df; fi ) || true
( if cd ${cyrus_sasl_xoauth2_idp_src_path}; then git clean -df; fi ) || true
( if cd ${scitokens_cpp_src_path}; then git clean -df; fi ) || true

for u in _gfarmmd _gfarmfs; do
  $USERADD "$u"
done

# for grid-cert-request
VARADM=/var/adm
VARADMWTMP=${VARADM}/wtmp
VARLOGMESSAGES=/var/log/messages
if [ ! -s $VARADMWTMP ]; then  # not exist or size is zero
    mkdir -p $VARADM && head -1000 /dev/urandom >> $VARADMWTMP
fi
if [ ! -s $VARLOGMESSAGES ]; then
    date >> $VARLOGMESSAGES
fi

# readable from users for grid-cert-request
chmod go+r $VARLOGMESSAGES

CA_DIR=/root/simple_ca
CA_CONF=${CA_DIR}/grid-ca-ssl.conf

### SEE ALSO: gen.sh
#CA_SUBJECT="cn=GlobusSimpleCA,ou=GlobusTest,o=Grid"
CA_SUBJECT="cn=GfarmCA,ou=GfarmDev,o=Gfarm"

grid-ca-create -pass "$ca_key_pass" -noint \
  -subject "${CA_SUBJECT}" -dir ${CA_DIR}

# enable "copy_extensions" to copy subjectAltName from CSR
mv ${CA_CONF} ${CA_CONF}_old
while read line; do
  echo "$line"
  if echo "$line" | grep '\[ CA_default \]' > /dev/null 2>&1 ; then
    echo "copy_extensions = copy"
  fi
done < ${CA_CONF}_old > ${CA_CONF}

ls globus_simple_ca_*.tar.gz \
  | sed -E 's/^globus_simple_ca_(.*)\.tar\.gz$/\1/' > /ca_hash

force_yes=y
MD=sha256

for i in $(seq 1 "$GFDOCKER_NUM_GFMDS"); do
  name="${GFDOCKER_HOSTNAME_PREFIX_GFMD}${i}"
  fqdn="${name}${GFDOCKER_HOSTNAME_SUFFIX}"
  ### -dns : use subjectAltName
  echo "$force_yes" \
    | grid-cert-request -verbose -nopw -prefix "$name" \
     -host "$fqdn" -dns "${fqdn},${name}" \
      -ca "$(cat /ca_hash)"
  grid-ca-sign -in "/etc/grid-security/${name}cert_request.pem" \
    -out "/etc/grid-security/${name}cert.pem" \
    -passin pass:"$ca_key_pass" -md $MD -dir ${CA_DIR}
  grid-cert-info -file "/etc/grid-security/${name}cert.pem"
done

### GFDOCKER_USE_SAN_FOR_GFSD:
### gfsd certificate with subjectAltName requires
### "export GLOBUS_GSSAPI_NAME_COMPATIBILITY=HYBRID" (or STRICT_GT2).
### (Maybe gfsd certificate should not use subjectAltName.)

for i in $(seq 1 "$GFDOCKER_NUM_GFSDS"); do
  name="${GFDOCKER_HOSTNAME_PREFIX_GFSD}${i}"
  fqdn="${name}${GFDOCKER_HOSTNAME_SUFFIX}"
  common_name="gfsd/${fqdn}"
  cert_path="/etc/grid-security/gfsd-${fqdn}"
  if [ ${GFDOCKER_USE_SAN_FOR_GFSD} -eq 1 ]; then
    ### -dns : use subjectAltName
    SAN_DNS="-dns ${fqdn},${name}"
  else
    SAN_DNS=""
  fi
  echo "$force_yes" \
    | grid-cert-request -verbose -nopw -prefix gfsd \
      -dir "$cert_path" -commonname "$common_name" -service gfsd \
      -host "$fqdn" ${SAN_DNS} \
      -ca "$(cat /ca_hash)"
  # openssl req -text -in "${cert_path}/gfsdcert_request.pem"
  grid-ca-sign -in "${cert_path}/gfsdcert_request.pem" \
    -out "${cert_path}/gfsdcert.pem" \
    -passin pass:"$ca_key_pass" -md $MD -dir ${CA_DIR}
  grid-cert-info -file "${cert_path}/gfsdcert.pem"
  chown -R _gfarmfs "$cert_path"

  GFSD_SBJ=$(grid-cert-info -subject -file "${cert_path}/gfsdcert.pem")
  ### unused
  echo "#${GFSD_SBJ} @host@ ${fqdn}" >> "$GRID_MAPFILE"
done

name="httpd"
fqdn="${name}${GFDOCKER_HOSTNAME_SUFFIX}"
echo "$force_yes" \
  | grid-cert-request -verbose -nopw -prefix "$name" \
   -host "$fqdn" -dns "${fqdn},${name}" \
    -ca "$(cat /ca_hash)"
grid-ca-sign -in "/etc/grid-security/${name}cert_request.pem" \
  -out "/etc/grid-security/${name}cert.pem" \
  -passin pass:"$ca_key_pass" -md $MD -dir ${CA_DIR}
grid-cert-info -file "/etc/grid-security/${name}cert.pem"

base_ssh_config="${gfarm_src_path}/docker/dev/common/ssh_config"
echo >> /etc/sudoers
echo '# for Gfarm' >> /etc/sudoers
for i in $(seq 1 "$GFDOCKER_NUM_USERS"); do
  user="${GFDOCKER_USERNAME_PREFIX}${i}"
  # User account is made by caller of this script.
  # see "Usage (Dockerfile)" for details.
  echo "${user} ALL=(root, _gfarmfs, _gfarmmd) NOPASSWD: /usr/bin/gfservice-agent" \
    >> /etc/sudoers
  echo "${user} ALL=(root, _gfarmfs, _gfarmmd) NOPASSWD: /usr/local/bin/gfservice-agent" \
    >> /etc/sudoers
  echo "### for regress" >> /etc/sudoers
  echo "${user} ALL=(_gfarmfs) NOPASSWD: ALL" >> /etc/sudoers
  echo "${user} ALL=NOPASSWD: ALL" >> /etc/sudoers
  ssh_dir="/home/${user}/.ssh"
  mkdir -m 0700 -p "$ssh_dir"
  ssh-keygen -f "${ssh_dir}/key-gfarm" -N ''
  authkeys="${ssh_dir}/authorized_keys"
  cp "${ssh_dir}/key-gfarm.pub" "$authkeys"
  chmod 0644 "$authkeys"
  ssh_config="${ssh_dir}/config"
  cp "$base_ssh_config" "$ssh_config"
  chmod 0644 "$ssh_config"
  mkdir -m 0700 -p "${ssh_dir}/ControlMasters"
  chown -R "${user}:${user}" "$ssh_dir"
  globus_dir="/home/${user}/.globus/"
  su - "$user" -c ' \
    grid-cert-request -verbose -cn "$(whoami)" -nopw -ca "$(cat /ca_hash)" \
  '
  grid-ca-sign -in "${globus_dir}/usercert_request.pem" \
    -out "${globus_dir}/usercert.pem" \
    -passin pass:"$ca_key_pass" -md $MD -dir ${CA_DIR}

  USER_SBJ=$(grid-cert-info -subject -file "${globus_dir}/usercert.pem")
  echo "${USER_SBJ} ${user}" >> "$GRID_MAPFILE"
  tls_dir="/home/${user}/.gfarm"
  ln -s ".globus" "$tls_dir"
done

# for TLS
mkdir -p /etc/pki/tls/certs
ln -s /etc/grid-security/certificates /etc/pki/tls/certs/gfarm

# make ${GFDOCKER_USERNAME_PREFIX}1 accesible to root@...
root_dotssh=/root/.ssh
root_authkey="${root_dotssh}/authorized_keys"
user1="${GFDOCKER_USERNAME_PREFIX}1"
user1_dotssh="/home/${user1}/.ssh"
user1_authkey="${user1_dotssh}/authorized_keys"
mkdir -p "${root_dotssh}"
cat "${user1_authkey}" >> "${root_authkey}"

base_gfservicerc="${gfarm_src_path}/docker/dev/common/rc.gfservice"
base_gfarm2rc="${gfarm_src_path}/docker/dev/common/rc.gfarm2rc"
for i in $(seq 1 "$GFDOCKER_NUM_USERS"); do
  user="${GFDOCKER_USERNAME_PREFIX}${i}"
  gfservicerc="/home/${user}/.gfservice"
  cp "$base_gfservicerc" "$gfservicerc"
  chmod 0644 "$gfservicerc"
  chown "${user}:${user}" "$gfservicerc"
  gfarm2rc="/home/${user}/.gfarm2rc"
  cp "$base_gfarm2rc" "$gfarm2rc"
  chmod 0644 "$gfarm2rc"
  chown "${user}:${user}" "$gfarm2rc"
  # use a symbolic link instead of direct referece to ${conf_include_dir},
  # to access ${HOME}/.gfarm2rc.passwd via relative pathname
  gfarm2rc_sasl="${gfarm2rc}.sasl"
  ln -s "${conf_include_dir}/auth-client.sasl.conf" "$gfarm2rc_sasl"
  gfarm2rc_passwd="${gfarm2rc}.passwd"
  cp /dev/null "$gfarm2rc_passwd"
  chmod 0600 "$gfarm2rc_passwd"
  chown "${user}:${user}" "$gfarm2rc_passwd"
  echo "sasl_user \"${user}@${SASL_DOMAIN}\"" >>"$gfarm2rc_passwd"
  echo "sasl_password \"${SASL_PASSWORD_BASE}${user}\"" >>"$gfarm2rc_passwd"
  if type saslpasswd2 2>/dev/null; then
    echo "${SASL_PASSWORD_BASE}${user}" |
      saslpasswd2 -c -u "${SASL_DOMAIN}" "${user}"
    if [ "${sasl_db:-NOT_SET}" = "NOT_SET" ]; then
      sasl_db=/etc/sasl2/sasldb2
      if [ ! -f ${sasl_db} ]; then
        sasl_db=/etc/sasldb2
      fi
    fi
    chown _gfarmfs "${sasl_db}"
  fi
  gfarm2rc_sasl_xoauth2="${gfarm2rc}.sasl.xoauth2"
  echo "sasl_user \"${user}\"" >>"$gfarm2rc_sasl_xoauth2"
  chown "${user}:${user}" "$gfarm2rc_sasl_xoauth2"
done

su - "$GFDOCKER_PRIMARY_USER" -c " \
  cd ~/gfarm \
    && for f in ~/gfarm/docker/dev/patch/*.patch; do \
      patch -p0 < \${f}; \
    done \
"

# install /usr/local/bin/authconfig
cp -p "${gfarm_src_path}/docker/dev/common/authconfig" /usr/local/bin/
# install /usr/local/bin/hookconfig
cp -p "${gfarm_src_path}/docker/dev/common/hookconfig" /usr/local/bin/

### for gfsd certificate ("CN=gfsd/... and subjectAltName")
if [ ${GFDOCKER_USE_SAN_FOR_GFSD} -eq 1 ]; then
  NAME_COMPATIBILITY_ENV="GLOBUS_GSSAPI_NAME_COMPATIBILITY=HYBRID"
  ### for "bash -l"
  #echo "export ${NAME_COMPATIBILITY_ENV}" >> /etc/profile.d/gfarm.sh
  ### for "ssh"
  #echo "${NAME_COMPATIBILITY_ENV}" >> /etc/environment

  ### system-wide configuration
  sed -i -e 's/^NAME_COMPATIBILITY=STRICT_RFC2818$/NAME_COMPATIBILITY=HYBRID/' /etc/grid-security/gsi.conf
fi

SYSTEMD_DIR=/etc/systemd/system
CLEAR_NOLOGIN=clear-nologin.service
CLEAR_NOLOGIN_SCRIPT=/root/clear-nologin.sh

cat <<EOF > ${SYSTEMD_DIR}/${CLEAR_NOLOGIN}
[Unit]
After=systemd-user-sessions.service

[Service]
User=root
ExecStart=${CLEAR_NOLOGIN_SCRIPT}

[Install]
WantedBy=multi-user.target
EOF

cat <<EOF > ${CLEAR_NOLOGIN_SCRIPT}
#!/bin/sh

while systemctl is-failed systemd-user-sessions; do
  systemctl start systemd-user-sessions
  sleep 1
done
exit 0
EOF

chmod +x ${CLEAR_NOLOGIN_SCRIPT}
systemctl enable ${CLEAR_NOLOGIN}


### setup autofs
GFARM2FS_OPT="auto_uid_min=40000,auto_uid_max=50000,auto_gid_min=40000,auto_gid_max=50000"
cat <<EOF | sudo dd of=/etc/auto.gfarm
ROOT -fstype=gfarm2fs,${GFARM2FS_OPT},gfarmfs_root=/,allow_other,default_permissions,username=${user1} :/home/${user1}/.gfarm2rc
* -fstype=gfarm2fs,${GFARM2FS_OPT},allow_root,username=& :/home/&/.gfarm2rc
EOF

cat <<EOF | sudo dd oflag=append conv=notrunc of=/etc/auto.master
/gfarm /etc/auto.gfarm --debug
EOF

cat <<EOF | sudo dd oflag=append conv=notrunc of=/etc/fuse.conf
user_allow_other
EOF

mkdir /gfarm
systemctl enable autofs

### disable getty
systemctl mask systemd-logind.service
systemctl mask getty.target
