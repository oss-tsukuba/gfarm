#!/bin/sh
set -eux

TENANT=$1

gfsudo gfmkdir /.tenants/$TENANT
gfuser -c admin+$TENANT admin-$TENANT / ""
#gfuser -A admin+$TENANT SASL $SASL_USER
gfgroup -c gfarmadm+$TENANT admin+$TENANT
gfgroup -c gfarmroot+$TENANT
gfsudo gfchown admin+$TENANT:gfarmadm+$TENANT /.tenants/$TENANT
