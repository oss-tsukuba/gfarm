#!/bin/sh
set -eu
set -x

# for Debian|Ubuntu
SYSTEM_CERT_DIR=/usr/local/share/ca-certificates
SYSTEM_CERT_UPDATE=update-ca-certificates
SYSTEM_CERT_BUNDLE=/etc/ssl/certs/ca-certificates.crt

CA_SRC=/myca.pem
CA_DST="${SYSTEM_CERT_DIR}/testca.crt"

VENV_DIR=/venv

if [ -f "$CA_SRC" ]; then
  cp -fp "$CA_SRC" "$CA_DST"
  chmod 644 "$CA_DST"
  chown root:root "$CA_DST"
fi

$SYSTEM_CERT_UPDATE

# for python-request
export REQUESTS_CA_BUNDLE=${SYSTEM_CERT_BUNDLE}
# for venv
. ${VENV_DIR}/bin/activate

exec "$@"
