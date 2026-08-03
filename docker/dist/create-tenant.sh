#!/bin/sh
set -eux

TENANT=$1
SASL_USER=$2

if ! gfuser gfarmadm+$TENANT >/dev/null 2>&1; then
    gfuser -c gfarmadm+$TENANT gfarmadm-$TENANT / ""
fi
gfuser -A gfarmadm+$TENANT SASL $SASL_USER

if ! gfgroup gfarmadm+$TENANT >/dev/null 2>&1; then
    gfgroup -c gfarmadm+$TENANT gfarmadm+$TENANT
fi
if ! gfgroup gfarmroot+$TENANT >/dev/null 2>&1; then
    gfgroup -c gfarmroot+$TENANT
fi

# Add an user for regression tests
: ${USER:=$(id -un)}
if ! gfuser ${USER}+$TENANT >/dev/null 2>&1; then
    gfuser -c ${USER}+$TENANT $USER / ""
fi

gfsudo gfmkdir -p /.tenants/$TENANT
gfsudo gfchown gfarmadm+$TENANT:gfarmadm+$TENANT /.tenants/$TENANT
