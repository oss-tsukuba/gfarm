#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

KEY=.gfarm_shared_key

# shared keys for system users
for u in _gfarmmd _gfarmfs; do
	sudo -u $u gfkey -f -p 31536000
	sudo cat /home/$u/$KEY | \
		gfarm-prun -stdin - "sudo -u $u tee /home/$u/$KEY > /dev/null"
	gfarm-prun -p sudo -u $u chmod 600 /home/$u/$KEY
done

status=0
