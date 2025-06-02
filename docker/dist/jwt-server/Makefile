TERM_INHERIT = $${TERM+--env TERM='${TERM}'}

login-jwt-server:
	docker exec $(TERM_INHERIT) -it gfarm-jwt-server /bin/bash

login-jwt-server2:
	docker exec $(TERM_INHERIT) -it gfarm-jwt-server2 /bin/bash

login-keycloak:
	docker exec $(TERM_INHERIT) -it gfarm-keycloak /bin/bash

login-rdesktop:
	docker exec $(TERM_INHERIT) -it gfarm-rdesktop /bin/bash

login-setup-keycloak:
	docker compose run $(TERM_INHERIT) --remove-orphans --rm -it setup-keycloak /bin/bash

build-jwt-server:
	docker compose build jwt-server --no-cache

build-setup-keycloak:
	docker compose build setup-keycloak --no-cache

build: build-jwt-server build-setup-keycloak

setup: setup-keycloak setup-jwt-server

setup-keycloak-old:
	docker exec gfarm-keycloak /keycloak/setup.sh

.PHONY: setup-keycloak
setup-keycloak:
	docker compose run --remove-orphans --rm -it setup-keycloak \
    python3 /setup-keycloak/keycloak-config.py /setup-keycloak/keycloak-config.yaml

setup-jwt-server:
	docker exec gfarm-jwt-server /jwt/setup_master1.sh
	docker exec gfarm-jwt-server2 /jwt/setup_master2.sh

up:
	docker compose up -d

down:
	docker compose down -v --remove-orphans

ps:
	docker compose ps
