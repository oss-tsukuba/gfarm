#!/bin/bash
set -euo pipefail

CONF="gfarm_sasl.conf"

echo "[INFO] deploying gfarm sasl config..."

if [[ ! -f "$CONF" ]]; then
    echo "[ERROR] config file not found: $CONF"
    exit 1
fi

source ./host.sh

for host in "${HOSTS[@]}"
do
  echo "Deploying to $host"

  scp -q "$CONF" $host:/tmp/gfarm.conf
  LIBDIR=$(pkg-config --variable=libdir libsasl2)

  ssh -n $host \
    "sudo cp /tmp/gfarm.conf ${LIBDIR}/sasl2/gfarm.conf"
done

echo "[INFO] deploy completed"
