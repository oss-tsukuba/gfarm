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
: $GFDOCKER_AUTH_TYPE
: $GFDOCKER_GFMD_JOURNAL_DIR
: $GFDOCKER_PRJ_NAME

### SEE ALSO: setup-univ.env
#ADMIN_DN="/O=Grid/OU=GlobusTest/OU=GlobusSimpleCA/CN=${GFDOCKER_PRIMARY_USER}"
ADMIN_DN="/O=Gfarm/OU=GfarmDev/OU=GfarmCA/CN=${GFDOCKER_PRIMARY_USER}"

IS_CGROUP_V2_COMMAND="${TOP}/docker/dev/common/is_cgroup_v2.sh"
IS_CGROUP_V2=true

CGROUP_V2_UNSUPPORTED="
centos7-src
centos7-pkg
"

if ${IS_CGROUP_V2_COMMAND}; then
  for name in $CGROUP_V2_UNSUPPORTED; do
    if [ "$name" = "$GFDOCKER_PRJ_NAME" ]; then
      echo 1>&2 "The container type ($name) is not supported, because systemd used in container on host OS enabled cgroup v2 requires version 247 or later."
      exit 1
    fi
  done
else
    IS_CGROUP_V2=false
fi

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
${gfmd}_CONFIG_GFARM_OPTIONS="-r -j ${GFDOCKER_GFMD_JOURNAL_DIR} -X -A \$LOGNAME -h \$gfmd${i} -a ${GFDOCKER_AUTH_TYPE} -D ${ADMIN_DN}"
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
gfsd${i}_CONFIG_GFSD_OPTIONS="-h \$gfsd${i} -l \$gfsd${i} -a ${GFDOCKER_PRJ_NAME}"
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

IS_CGROUP_V2="${IS_CGROUP_V2}" \
"${TOP}/docker/dev/common/gen_docker_compose_conf.py" \
  > "${COMPOSE_YAML}.tmp"
mv "${COMPOSE_YAML}.tmp" "${COMPOSE_YAML}"
