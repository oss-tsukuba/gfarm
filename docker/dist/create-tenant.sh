#!/bin/sh
set -eux

TENANT=$1
SASL_USER=$2

gfuser -c gfarmadm+$TENANT gfarmadm-$TENANT / ""
gfuser -A gfarmadm+$TENANT SASL $SASL_USER
gfgroup -c gfarmadm+$TENANT gfarmadm+$TENANT
gfgroup -c gfarmroot+$TENANT
gfsudo gfmkdir /.tenants/$TENANT
gfsudo gfchown gfarmadm+$TENANT:gfarmadm+$TENANT /.tenants/$TENANT
