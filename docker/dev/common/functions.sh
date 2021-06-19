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
