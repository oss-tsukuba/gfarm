set -eu

: $TOP
: $GFDOCKER_PRIMARY_USER
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
: $GFDOCKER_AUTH_TYPE
: $GFDOCKER_GFMD_JOURNAL_DIR
: $GFDOCKER_PRJ_NAME

gen_gfservicerc() {
  cat <<EOF
# This file was automatically generated.

LOGNAME=${GFDOCKER_PRIMARY_USER}
EOF

  for i in $(seq 1 "$GFDOCKER_NUM_GFMDS"); do
    gfmd="${GFDOCKER_HOSTNAME_PREFIX_GFMD}${i}"
    cat <<EOF

## *_AUTH_TYPE=sharedsecret can copy ~/.gfarm_shared_key

##
## gfmd ${i}
##
gfmd${i}=${gfmd}${GFDOCKER_HOSTNAME_SUFFIX}
${gfmd}_CONFIG_GFARM_OPTIONS="-r -j ${GFDOCKER_GFMD_JOURNAL_DIR} -X -A \$LOGNAME -h \$gfmd${i} -a ${GFDOCKER_AUTH_TYPE} -D /O=Grid/OU=GlobusTest/OU=GlobusSimpleCA/CN=${GFDOCKER_PRIMARY_USER}"
gfmd${i}_AUTH_TYPE=sharedsecret
EOF
  done

  for i in $(seq 1 "$GFDOCKER_NUM_GFSDS"); do
    gfsd="${GFDOCKER_HOSTNAME_PREFIX_GFSD}${i}"
    cat <<EOF


##
## gfsd ${i}
##
gfsd${i}=${gfsd}${GFDOCKER_HOSTNAME_SUFFIX}
gfsd${i}_CONFIG_GFSD_OPTIONS="-h \$gfsd${i} -l \$gfsd${i} -a docker"
gfsd${i}_AUTH_TYPE=sharedsecret
EOF
  done

  for i in $(seq 1 "$GFDOCKER_NUM_CLIENTS"); do
    client="${GFDOCKER_HOSTNAME_PREFIX_CLIENT}${i}"
    cat <<EOF


##
## client ${i}
##
client${i}=${client}${GFDOCKER_HOSTNAME_SUFFIX}
client${i}_AUTH_TYPE=sharedsecret
EOF
  done
}

COMPOSE_YAML="${TOP}/docker/dev/docker-compose.yml"

gen_gfservicerc > "${TOP}/docker/dev/common/rc.gfservice"
"${TOP}/docker/dev/common/gen_docker_compose_conf.py" \
  > "${COMPOSE_YAML}.tmp"
mv "${COMPOSE_YAML}.tmp" "${COMPOSE_YAML}"
