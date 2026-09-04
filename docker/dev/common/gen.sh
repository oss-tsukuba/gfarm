set -eu

: $TOP
: $GFDOCKER_PRIMARY_USER
: $GFDOCKER_PRIMARY_UID
: $GFDOCKER_TENANT_ADMIN_USER
: $GFDOCKER_TENANT_ADMIN_UID
: $GFDOCKER_NUM_GFMDS
: $GFDOCKER_NUM_GFSDS
: $GFDOCKER_NUM_CLIENTS
: $GFDOCKER_IP_VERSION
: $GFDOCKER_SUBNET
: $GFDOCKER_START_HOST_ADDR
: $GFDOCKER_HOSTNAME_PREFIX_GFMD
: $GFDOCKER_HOSTNAME_PREFIX_GFSD
: $GFDOCKER_HOSTNAME_PREFIX_CLIENT
: $GFDOCKER_HOSTNAME_SUFFIX
: $GFDOCKER_GFMD_JOURNAL_DIR
: $GFDOCKER_PRJ_NAME

### SEE ALSO: setup-univ.env
#ADMIN_DN="/O=Grid/OU=GlobusTest/OU=GlobusSimpleCA/CN=${GFDOCKER_PRIMARY_USER}"
ADMIN_DN="/O=Gfarm/OU=GfarmDev/OU=GfarmCA/CN=${GFDOCKER_PRIMARY_USER}"
INIT_AUTH_TYPE=sharedsecret

gen_gfservicerc() {
  cat <<EOF
# This file was automatically generated.

LOGNAME=${GFDOCKER_PRIMARY_USER}
EOF

  for i in $(seq 1 "$GFDOCKER_NUM_GFMDS"); do
    gfmd="${GFDOCKER_HOSTNAME_PREFIX_GFMD}${i}"
    gfmd_options="-r -j ${GFDOCKER_GFMD_JOURNAL_DIR}"
    gfmd_options="${gfmd_options} -X -A \$LOGNAME"
    gfmd_options="${gfmd_options} -h \$gfmd${i}"
    gfmd_options="${gfmd_options} -a ${INIT_AUTH_TYPE}"
    gfmd_options="${gfmd_options} -D ${ADMIN_DN}"
    cat <<EOF

## if *_AUTH_TYPES contain sharedsecret or tls_sharedsecret,
## ~/.gfarm_shared_key can be copied
## if only sasl is specified by -a option of *_CONFIG_GFARM_OPTIONS,
## tls_client_certificate will also be added to gfarm2.conf on all hosts

##
## gfmd ${i}
##
gfmd${i}=${gfmd}${GFDOCKER_HOSTNAME_SUFFIX}
${gfmd}_CONFIG_GFARM_OPTIONS="${gfmd_options}"
gfmd${i}_AUTH_TYPES=sharedsecret
EOF
  done

  for i in $(seq 1 "$GFDOCKER_NUM_GFSDS"); do
    gfsd="${GFDOCKER_HOSTNAME_PREFIX_GFSD}${i}"
    cat <<EOF


##
## gfsd ${i}
##
gfsd${i}=${gfsd}${GFDOCKER_HOSTNAME_SUFFIX}
gfsd${i}_CONFIG_GFSD_OPTIONS="-h \$gfsd${i} -l \$gfsd${i} -a ${GFDOCKER_PRJ_NAME}"
gfsd${i}_AUTH_TYPES=sharedsecret
EOF
  done

  for i in $(seq 1 "$GFDOCKER_NUM_CLIENTS"); do
    client="${GFDOCKER_HOSTNAME_PREFIX_CLIENT}${i}"
    cat <<EOF


##
## client ${i}
##
client${i}=${client}${GFDOCKER_HOSTNAME_SUFFIX}
client${i}_AUTH_TYPES=sharedsecret
EOF
  done
}

COMPOSE_YAML="${TOP}/docker/dev/docker-compose.yml"

gen_gfservicerc > "${TOP}/docker/dev/common/rc.gfservice"

"${TOP}/docker/dev/common/gen_docker_compose_conf.py" \
  > "${COMPOSE_YAML}.tmp"
mv "${COMPOSE_YAML}.tmp" "${COMPOSE_YAML}"
