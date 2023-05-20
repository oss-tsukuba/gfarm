#!/bin/sh
set -eu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

REQ=$1
CERT=$2

PASS=globus
DIGEST=sha256

echo $PASS | sudo grid-ca-sign -in $REQ -out $CERT -passin stdin -md $DIGEST

status=0
