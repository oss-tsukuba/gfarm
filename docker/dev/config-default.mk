#GFDOCKER_PROXY_HOST = 192.168.0.1
#GFDOCKER_PROXY_PORT = 8080
GFDOCKER_PROXY_HOST =
GFDOCKER_PROXY_PORT =
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
GFDOCKER_SUBNET = 192.168.224.0/24
GFDOCKER_START_HOST_ADDR = 11

# syntax: [A-Za-z0-9]([A-Za-z0-9]|-|_)*
GFDOCKER_HOSTNAME_PREFIX_GFMD = gfmd
GFDOCKER_HOSTNAME_PREFIX_GFSD = gfsd
GFDOCKER_HOSTNAME_PREFIX_CLIENT = client

# syntax: sharedsecret, gsi or gsi_auth
GFDOCKER_AUTH_TYPE = gsi_auth

# --no-cache for docker build (0: disable, 1: enable)
GFDOCKER_NO_CACHE = 0
SUDO = sudo

# gfarm-s3 users
GFDOCKER_GFARMS3_USERS_DEFAULT = user1:user1:K4XcKzocrUhrnCAKrx2Z user2:user2:qzjjOwLjeWptTFUZVThs
GFDOCKER_GFARMS3_MYPROXY_SERVER_DEFAULT = 
GFDOCKER_GFARMS3_SHARED_DIR_DEFAULT = /share

GFDOCKER_GFARMS3_CACHE_BASEDIR_COMMON = /mnt/cache
GFDOCKER_GFARMS3_CACHE_SIZE_COMMON = 1024
GFDOCKER_GFARMS3_WSGI_HOMEDIR_COMMON = /home/wsgi
GFDOCKER_GFARMS3_WSGI_USER_COMMON = wsgi
GFDOCKER_GFARMS3_WSGI_GROUP_COMMON = wsgi
GFDOCKER_GFARMS3_WSGI_PORT_COMMON = 8000