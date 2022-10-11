#! /bin/sh

# Usage (Dockerfile):
#
# ENV OPENSSL_PACKAGE_NAME (optional)
# ARG GFDOCKER_NUM_JOBS
# ARG GFDOCKER_PRIMARY_USER
# ARG GFDOCKER_ENABLE_PROXY

# for SASL (optional)
# ARG GFDOCKER_SASL_MECH_LIST (optional)
# ARG GFDOCKER_SASL_LOG_LEVEL (optional)
# ARG GFDOCKER_SASL_XOAUTH2_SCOPE (optional)
# ARG GFDOCKER_SASL_XOAUTH2_AUD (optional)
# ARG GFDOCKER_SASL_XOAUTH2_USER_CLAIM (optional)

# RUN "/home/${GFDOCKER_PRIMARY_USER}/gfarm/docker/dev/common/make-install-univ.sh"

set -eux

GFDOCKER_USE_TSAN=0

: ${OPENSSL_PACKAGE_NAME:=}
: $GFDOCKER_NUM_JOBS
: $GFDOCKER_PRIMARY_USER
: ${GFDOCKER_ENABLE_PROXY:=false}

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

scitokens_prefix=/usr/local

su - "$GFDOCKER_PRIMARY_USER" -c " \
  cd ~/gfarm \
    && (test -f Makefile && make distclean || true) \
    && eval \"${CFLAGS_ARGS}\" ./configure ${GFARM_OPT} \
    && make -j '${GFDOCKER_NUM_JOBS}' \
" \
  && cd "/home/${GFDOCKER_PRIMARY_USER}/gfarm" \
  && make install || exit 1

su - "$GFDOCKER_PRIMARY_USER" -c " \
  cd ~/gfarm2fs \
    && (test -f Makefile && make distclean || true) \
    && eval \"${CFLAGS_ARGS}\" ./configure --with-gfarm=/usr/local \
    && make -j '${GFDOCKER_NUM_JOBS}' \
" \
  && cd "/home/${GFDOCKER_PRIMARY_USER}/gfarm2fs" \
  && make install || exit 1

su - "$GFDOCKER_PRIMARY_USER" -c " \
  cd ~/gfarm2fs \
    && (test -f Makefile && make distclean || true) \
    && eval \"${CFLAGS_ARGS}\" ./configure --with-gfarm=/usr/local \
    && make -j '${GFDOCKER_NUM_JOBS}' \
" \
  && cd "/home/${GFDOCKER_PRIMARY_USER}/gfarm2fs" \
  && make install || exit 1

if [ -d "/home/${GFDOCKER_PRIMARY_USER}/jwt-logon" ]; then
  cd "/home/${GFDOCKER_PRIMARY_USER}/jwt-logon" &&
  make PREFIX=/usr/local install || exit 1
fi

# if proxy is set, the following fails for some unknown reason,
# the error is:
#	go: golang.org/x/crypto@v0.0.0-20220722155217-630584e8d5aa: Get "https://proxy.golang.org/golang.org/x/crypto/@v/v0.0.0-20220722155217-630584e8d5aa.info": dial tcp: lookup proxy.golang.org on 8.8.4.4:53: read udp 172.17.0.2:40024->8.8.4.4:53: i/o timeout
if  [ -d "/home/${GFDOCKER_PRIMARY_USER}/jwt-agent" ] && type go 2>/dev/null &&
    ! "${GFDOCKER_ENABLE_PROXY}"
then
  su - "$GFDOCKER_PRIMARY_USER" -c "cd ~/jwt-agent && make" &&
    cd "/home/${GFDOCKER_PRIMARY_USER}/jwt-agent" &&
    make PREFIX=/usr/local install || exit 1
fi

if [ -d "/home/${GFDOCKER_PRIMARY_USER}/scitokens-cpp" -a \
     -f /usr/include/sqlite3.h -a \
     -f /usr/include/curl/curl.h -a \
     -f /usr/include/uuid/uuid.h ] &&
   type c++ 2>/dev/null &&
   type cmake 2>/dev/null
then
  su - "$GFDOCKER_PRIMARY_USER" -c "cd ~/scitokens-cpp &&
      mkdir -p build &&
      cd build &&
      cmake -DCMAKE_INSTALL_PREFIX="${scitokens_prefix}" .. &&
      make -j '${GFDOCKER_NUM_JOBS}'" &&
    cd "/home/${GFDOCKER_PRIMARY_USER}/scitokens-cpp/build" &&
    make install || exit 1
fi


# autoconf at least until version 2.69 does not detect /usr/lib64 automatically
# as ${libdir}, thus detect it by myself
if [ -f /usr/lib64/sasl2/libsasldb.so ]; then
  sasl_libdir=/usr/lib64
elif [ -f /usr/lib/sasl2/libsasldb.so ]; then
  sasl_libdir=/usr/lib
else
  sasl_libdir=
fi
if [ -d "/home/${GFDOCKER_PRIMARY_USER}/cyrus-sasl-xoauth2-idp" -a \
     -n "${sasl_libdir}" -a -f /usr/include/sasl/sasl.h -a \
     -f ${scitokens_prefix}/include/scitokens/scitokens.h -a \
     "${GFDOCKER_SASL_MECH_LIST+set}" = "set" ]
then
  # NOTE: this installs to /usr/lib64/sasl2/ instead of /usr/local/lib64/sasl2/
  su - "$GFDOCKER_PRIMARY_USER" -c "cd ~/cyrus-sasl-xoauth2-idp &&
      ./autogen.sh &&
      ./configure --libdir="${sasl_libdir}" &&
      make" &&
    cd "/home/${GFDOCKER_PRIMARY_USER}/cyrus-sasl-xoauth2-idp" &&
    make install || exit 1
  (
    cat <<_EOF_
mech_list: ${GFDOCKER_SASL_MECH_LIST}
log_level: ${GFDOCKER_SASL_LOG_LEVEL}
xoauth2_scope: ${GFDOCKER_SASL_XOAUTH2_SCOPE}
xoauth2_aud: ${GFDOCKER_SASL_XOAUTH2_AUD}
xoauth2_user_claim: ${GFDOCKER_SASL_XOAUTH2_USER_CLAIM}
_EOF_

    if "${GFDOCKER_ENABLE_PROXY}"; then
      cat <<_EOF_
proxy: ${https_proxy}
_EOF_
    fi
  ) >"${sasl_libdir}/sasl2/gfarm.conf"
fi

# for autofs
cp $(which mount.gfarm2fs) /sbin/
