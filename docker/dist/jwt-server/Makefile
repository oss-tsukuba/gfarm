TERM_INHERIT = $${TERM+--env TERM='${TERM}'}

login-jwt-server:
	docker exec $(TERM_INHERIT) -it gfarm-jwt-server /bin/bash

login-jwt-server2:
	docker exec $(TERM_INHERIT) -it gfarm-jwt-server2 /bin/bash

login-keycloak:
	docker exec $(TERM_INHERIT) -it gfarm-keycloak /bin/bash

login-rdesktop:
	docker exec $(TERM_INHERIT) -it gfarm-rdesktop /bin/bash

setup: setup-keycloak setup-jwt-server

setup-keycloak:
	docker exec gfarm-keycloak /keycloak/setup.sh

setup-jwt-server:
	docker exec gfarm-jwt-server /setup.sh
	docker exec gfarm-jwt-server2 /setup_slave.sh
