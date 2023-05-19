MNTDIR=/mnt
MNTDIR_UID=$(stat -c %u ${MNTDIR})
MNTDIR_GID=$(stat -c %g ${MNTDIR})
WORKDIR=${MNTDIR}/work
PACKAGES_STORE_DIR=${MNTDIR}/packages

SUDO=sudo
if [ $(id -u) -eq 0 ]; then
    SUDO=
fi

save_package() {
    FILES="$1"
    OUTDIR="${PACKAGES_STORE_DIR}/${GFDOCKER_PRJ_NAME}"

    if [ ! -d "${OUTDIR}" ]; then
        ${SUDO} mkdir -p "${OUTDIR}"
        ${SUDO} chown ${MNTDIR_UID}:${MNTDIR_GID} "${OUTDIR}"
    fi
    ${SUDO} cp ${FILES} "${OUTDIR}"
    ${SUDO} chown ${MNTDIR_UID}:${MNTDIR_GID} "${OUTDIR}"/*
}

gfuser_from_index() {
    tenant_index=$1
    user_index=$2

    case $tenant_index in
    1) # i.e. default tenant
       tenant_user_suffix=;;
    2) # for testing, use same user_index with the default tenant
       ;;
    *) user_index=$(((tenant_index - 1) * GFDOCKER_NUM_USERS + user_index));;
    esac

    echo "${GFDOCKER_USERNAME_PREFIX}${user_index}"
}

gftenant_user_suffix_from_index() {
    tenant_index=$1

    case $tenant_index in
    1) # i.e. default tenant
       tenant_user_suffix=;;
    *) tenant_name="${GFDOCKER_TENANTNAME_PREFIX}${tenant_index}"
       tenant_user_suffix="+${tenant_name}"
       ;;
    esac

    echo "${tenant_user_suffix}"
}

gftenant_path_prefix_from_index() {
    tenant_index=$1

    case $tenant_index in
    1) # i.e. default tenant
       tenant_path_prefix=;;
    *) tenant_name="${GFDOCKER_TENANTNAME_PREFIX}${tenant_index}"
       tenant_path_prefix="/.tenants/${tenant_name}"
       ;;
    esac

    echo "${tenant_path_prefix}"
}

gfhome_from_index() {
  echo >&2 "ABORT!! gfhome_from_index is called"
  exit 2
}
