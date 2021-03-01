COMPOSE_PROJECT_NAME="gfarm-dev-${NAME}"

SUDO=sudo
COMPOSE="${SUDO} COMPOSE_PROJECT_NAME=${COMPOSE_PROJECT_NAME} docker-compose -f docker-compose.${NAME}.yml"
