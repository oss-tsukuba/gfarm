#!/bin/sh

LOCALCONFIG="gfmd-failover-local.sh"

if [ ! -e "$LOCALCONFIG" ]; then
	echo ERROR: $LOCALCONFIG is not found.
	exit 1
fi
. ./$LOCALCONFIG
