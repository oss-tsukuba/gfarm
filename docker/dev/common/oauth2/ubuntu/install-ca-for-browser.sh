#!/bin/bash

set -eu
set -x

CA=/mnt/desktop/cacert.pem
CA_NAME=GFARM_DEV_CA

NSSDB_CHROME=${HOME}/.pki/nssdb
NSSDB_CHROMIUM=$(dirname $(find ${HOME}/snap/chromium/ | grep .pki/nssdb/pkcs11.txt || true) || true)
NSSDB_FIREFOX=$(dirname $(find ${HOME}/.mozilla/firefox/*.default-release/pkcs11.txt || true) || true)
for d in $NSSDB_CHROME $NSSDB_CHROMIUM $NSSDB_FIREFOX; do
    if [ -d "$d" ]; then
        certutil -A -d sql:${d} -n $CA_NAME -t C,, -i $CA
    fi
done
