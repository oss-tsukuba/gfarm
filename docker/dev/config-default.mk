TZ = Asia/Tokyo
#LANG = ja_JP.UTF-8
LANG = en_US.UTF-8

DOCKER_CMD = docker
#DOCKER_CMD = podman

### Compose v2
DOCKER_COMPOSE_CMD = docker compose
### Podman Compose
#DOCKER_COMPOSE_CMD = podman-compose
### Compose v1 (obsolete)
#DOCKER_COMPOSE_CMD = docker-compose
### Compose v1 running on container for Podman (obsolete)
#DOCKER_COMPOSE_CMD = $(ROOTDIR)/common/compose.sh

#GFDOCKER_PROXY_HOST = 192.168.0.1
#GFDOCKER_PROXY_PORT = 8080
GFDOCKER_PROXY_HOST =
GFDOCKER_PROXY_PORT =
GFDOCKER_NO_PROXY = localhost,127.0.0.1
GFDOCKER_NUM_JOBS = 8

# number of containers
#GFDOCKER_NUM_GFMDS >= 1
#GFDOCKER_NUM_GFSDS >= 1
#GFDOCKER_NUM_CLIENTS >= 1
GFDOCKER_NUM_GFMDS = 3
GFDOCKER_NUM_GFSDS = 4
GFDOCKER_NUM_CLIENTS = 2

# number of local/global accounts
GFDOCKER_NUM_USERS = 4

# requirements:
#GFDOCKER_IP_VERSION: syntax: 4 or 6
#GFDOCKER_SUBNET: syntax: ${ip_address}/${prefix}
#GFDOCKER_START_HOST_ADDR: syntax: intager
GFDOCKER_IP_VERSION = 4
#GFDOCKER_SUBNET = 10.2.3.0/24
GFDOCKER_SUBNET = 192.168.224.0/24
GFDOCKER_START_HOST_ADDR = 11

# syntax: [A-Za-z0-9]([A-Za-z0-9]|-|_)*
GFDOCKER_HOSTNAME_PREFIX_GFMD = gfmd
GFDOCKER_HOSTNAME_PREFIX_GFSD = gfsd
GFDOCKER_HOSTNAME_PREFIX_CLIENT = client

#GFDOCKER_HOSTNAME_SUFFIX =
#GFDOCKER_HOSTNAME_SUFFIX = .example.com
GFDOCKER_HOSTNAME_SUFFIX = .test

GFDOCKER_USE_SAN_FOR_GFSD = 1
#GFDOCKER_USE_SAN_FOR_GFSD = 0

# qemu-user-static is used if not empty
GFDOCKER_PLATFORM =
#GFDOCKER_PLATFORM = linux/arm64
#GFDOCKER_PLATFORM = linux/arm/v7
#GFDOCKER_PLATFORM = linux/amd64
#GFDOCKER_PLATFORM = linux/386
#GFDOCKER_PLATFORM = linux/ppc64le

# syntax: sharedsecret, gsi, gsi_auth, tls_sharedsecret or
#		tls_client_certificate
GFDOCKER_AUTH_TYPE = gsi_auth

# GFDOCKER_GFMD_JOURNAL_DIR = /var/gfarm-metadata/journal/
GFDOCKER_GFMD_JOURNAL_DIR = /dev/shm/gfarm-metadata/journal/

# --no-cache for docker build (0: disable, 1: enable)
GFDOCKER_NO_CACHE = 0
SUDO = sudo

# cyrus-sasl-xoauth2-idp
#
# GFDOCKER_SASL_MECH_LIST:
# 	tested mechanisms: ANONYMOUS LOGIN PLAIN XOAUTH2
#	NOTE: PLAIN and XOAUTH2 are mandatory, do not remove them.
GFDOCKER_SASL_MECH_LIST = PLAIN XOAUTH2
GFDOCKER_SASL_LOG_LEVEL = 7
GFDOCKER_SASL_XOAUTH2_SCOPE = hpci
GFDOCKER_SASL_XOAUTH2_AUD = hpci
GFDOCKER_SASL_XOAUTH2_USER_CLAIM = sub

# using port numbers of host OS
GFDOCKER_HOSTPORT_S3_HTTP = 18080
GFDOCKER_HOSTPORT_S3_HTTPS = 18443
GFDOCKER_HOSTPORT_S3_DIRECT = 18888

# gfarm-s3 users
GFDOCKER_GFARMS3_USERS = user1:user1:K4XcKzocrUhrnCAKrx2Z user2:user2:qzjjOwLjeWptTFUZVThs
GFDOCKER_GFARMS3_SECRET_USER1 = xNMOFbCLhu+FKz1VmIDgHwPUQad0h1arCpdJAiN1oih1gNUx
GFDOCKER_GFARMS3_SECRET_USER2 = DzZv57R8wBIuVZdtAkE1uK1HoebLPMzKM6obA4IDqOhaLIBf

# front web proxy server for gfarm-s3 (apache or nginx)
#GFDOCKER_GFARMS3_FRONT_WEBSERVER=apache
GFDOCKER_GFARMS3_FRONT_WEBSERVER=nginx

GFDOCKER_GFARMS3_MYPROXY_SERVER =
GFDOCKER_GFARMS3_SHARED_DIR = /share
