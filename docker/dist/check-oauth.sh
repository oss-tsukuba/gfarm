#!/bin/sh
set -xeu

: ${USER:=$(id -un)}
PASS=$(sh ./init-jwt.sh)
echo $PASS | jwt-agent -s http://jwt-server -l user1
gfuser -A $USER SASL user1
echo "*** oauth2 ***"
sh ./edconf.sh oauth2 > /dev/null
sh ./check.sh
