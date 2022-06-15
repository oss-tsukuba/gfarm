#! /bin/sh

# Usage (Dockerfile):
#
# ENV OPENSSL_PACKAGE_NAME (optional)
# ARG GFDOCKER_NUM_JOBS
# ARG GFDOCKER_PRIMARY_USER
# RUN "/home/${GFDOCKER_PRIMARY_USER}/gfarm/docker/dev/common/make-install-univ.sh"

set -eux

GFDOCKER_USE_TSAN=0

: ${OPENSSL_PACKAGE_NAME:=}
: $GFDOCKER_NUM_JOBS
: $GFDOCKER_PRIMARY_USER

WITH_OPENSSL_OPT=
if [ -n "${OPENSSL_PACKAGE_NAME}" ]; then
    WITH_OPENSSL_OPT="--with-openssl=${OPENSSL_PACKAGE_NAME}"
fi

if [ "$GFDOCKER_USE_TSAN" -eq 1 ]; then
  CFLAGS_ARGS="CFLAGS=\\\"-fsanitize=thread -fPIE -pie -g -Wall\\\""
else
  CFLAGS_ARGS=""
fi

GFARM_OPT="--with-globus=/usr --enable-xmlattr ${WITH_OPENSSL_OPT}"

su - "$GFDOCKER_PRIMARY_USER" -c " \
  cd ~/gfarm \
    && (test -f Makefile && make distclean || true) \
    && eval \"${CFLAGS_ARGS}\" ./configure ${GFARM_OPT} \
    && make -j '${GFDOCKER_NUM_JOBS}' \
" \
  && cd "/home/${GFDOCKER_PRIMARY_USER}/gfarm" \
  && make -j install || exit 1

su - "$GFDOCKER_PRIMARY_USER" -c " \
  cd ~/gfarm2fs \
    && (test -f Makefile && make distclean || true) \
    && eval \"${CFLAGS_ARGS}\" ./configure --with-gfarm=/usr/local \
    && make -j '${GFDOCKER_NUM_JOBS}' \
" \
  && cd "/home/${GFDOCKER_PRIMARY_USER}/gfarm2fs" \
  && make -j install || exit 1

# for autofs
cp $(which mount.gfarm2fs) /sbin/
