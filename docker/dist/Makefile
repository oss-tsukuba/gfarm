login:
	docker exec -u ${USER} -w /home/${USER}/gfarm/docker/dist -it gfarm-c1 /bin/bash

login-jwt-server:
	docker exec -it gfarm-jwt-server /bin/bash

login-jwt-server2:
	docker exec -it gfarm-jwt-server2 /bin/bash

login-keycloak:
	docker exec -it gfarm-keycloak /bin/bash

login-minica:
	docker exec -it gfarm-minica /bin/bash

login-rdesktop:
	docker exec -it gfarm-rdesktop /bin/bash

setup: setup-keycloak setup-jwt-server

setup-keycloak:
	docker exec gfarm-keycloak /keycloak/setup.sh

setup-jwt-server:
	docker exec gfarm-jwt-server /setup.sh
	docker exec gfarm-jwt-server2 /setup_slave.sh

