login:
	docker exec -u ${USER} -w /home/${USER}/gfarm/docker/dist -it gfarm-c1 /bin/bash

down:
	(cd jwt-server; docker compose down)
	(cd minica; docker compose down)
	docker compose down
