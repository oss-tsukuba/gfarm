#!/bin/sh

# ARG GFDOCKER_ENABLE_PROXY

set -eux

GFDOCKER_SCRIPT_PATH="`dirname $0`"
name=jwt-agent

# if proxy is set, the following fails for some unknown reason,
# the error is:
#	go: golang.org/x/crypto@v0.0.0-20220722155217-630584e8d5aa: Get "https://proxy.golang.org/golang.org/x/crypto/@v/v0.0.0-20220722155217-630584e8d5aa.info": dial tcp: lookup proxy.golang.org on 8.8.4.4:53: read udp 172.17.0.2:40024->8.8.4.4:53: i/o timeout
if "${GFDOCKER_ENABLE_PROXY:-false}"
then
  echo >&2 "$0: building ${name} is not supported when web proxy is used"
  exit 0	# this is OK at least for now
fi

"${GFDOCKER_SCRIPT_PATH}/rpm-build.sh" -O -T "go" "${name}"
