#! /bin/sh

# Usage (Dockerfile):
#
# ARG GFDOCKER_NUM_JOBS
# ARG GFDOCKER_PRIMARY_USER
# RUN "/home/${GFDOCKER_PRIMARY_USER}/gfarm/docker/dev/common/make-install-univ.sh"

set -eux

GFDOCKER_USE_TSAN=0

: $GFDOCKER_NUM_JOBS
: $GFDOCKER_PRIMARY_USER

if [ "$GFDOCKER_USE_TSAN" -eq 1 ]; then
  CFLAGS_ARGS="CFLAGS=\\\"-fsanitize=thread -fPIE -pie -g -Wall\\\""
else
  CFLAGS_ARGS=""
fi

su - "$GFDOCKER_PRIMARY_USER" -c " \
  cd ~/gfarm \
    && (test -f Makefile && make distclean || true) \
    && eval \"${CFLAGS_ARGS}\" ./configure --with-globus=/usr --enable-xmlattr \
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
