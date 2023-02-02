#!/bin/sh

set -eux

GFDOCKER_SCRIPT_PATH="`dirname $0`"
"${GFDOCKER_SCRIPT_PATH}/rpm-install.sh" -O cyrus-sasl-xoauth2-idp
