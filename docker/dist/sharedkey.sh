#!/bin/sh
set -xeu
status=1
trap 'echo NG; exit $status' 1 2 15

gfkey -f -p 31536000
KEY=.gfarm_shared_key
gfarm-pcp -p ~/$KEY .

# shared keys for system users
for u in _gfarmmd _gfarmfs; do
	sudo -u $u gfkey -f -p 31536000
	sudo cat /home/$u/$KEY | \
		gfarm-prun -stdin - "sudo -u $u tee /home/$u/$KEY > /dev/null"
	gfarm-prun -p sudo -u $u chmod 600 /home/$u/$KEY
done

echo Done
