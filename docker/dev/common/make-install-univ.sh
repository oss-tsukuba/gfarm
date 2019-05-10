#! /bin/sh

# Usage (Dockerfile):
#
# ARG GFDOCKER_NUM_JOBS
# ARG GFDOCKER_PRIMARY_USER
# RUN "/home/${GFDOCKER_PRIMARY_USER}/gfarm/docker/dev/common/make-install-univ.sh"

set -eux

: $GFDOCKER_NUM_JOBS
: $GFDOCKER_PRIMARY_USER

su - "$GFDOCKER_PRIMARY_USER" -c " \
  cd ~/gfarm \
    && ./configure --with-globus --enable-xmlattr \
    && make -j '${GFDOCKER_NUM_JOBS}' \
" \
  && cd "/home/${GFDOCKER_PRIMARY_USER}/gfarm" \
  && make install

su - "$GFDOCKER_PRIMARY_USER" -c " \
  cd ~/gfarm2fs \
    && ./configure --with-gfarm=/usr/local \
    && make -j '${GFDOCKER_NUM_JOBS}' \
" \
  && cd "/home/${GFDOCKER_PRIMARY_USER}/gfarm2fs" \
  && make install
