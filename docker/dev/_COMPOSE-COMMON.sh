COMPOSE_PROJECT_NAME="gfarm-dev-${NAME}"

SUDO=sudo
# address of host OS to publish ports.  specify 0.0.0.0 to access them
# from other hosts.
: ${GFDOCKER_HOSTPORT_ADDR:=127.0.0.1}

COMPOSE="${SUDO} COMPOSE_PROJECT_NAME=${COMPOSE_PROJECT_NAME}"
COMPOSE="${COMPOSE} GFDOCKER_HOSTPORT_ADDR=${GFDOCKER_HOSTPORT_ADDR}"
COMPOSE="${COMPOSE} docker-compose -f docker-compose.${NAME}.yml"
