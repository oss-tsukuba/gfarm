#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; rm -f ~/local/gfarm2.conf; exit $status' \
	0 1 2 15

# master metadata server
: ${USER:=$(id -un)}
DN=$(grid-proxy-info -issuer)
[ X"$DN" = X ] && exit 1
CONFIG_OPTIONS="-A $USER -r -X -d sha1 -a gsi -D $DN"
sudo config-gfarm -N $CONFIG_OPTIONS

# update gfmd.conf
for d in /etc /usr/local/etc
do
	[ -f $d/gfmd.conf ] && break
done
[ -f $d/gfmd.conf ] && CONFDIR=$d || exit 1
cat <<_EOF_ | sudo tee -a $CONFDIR/gfmd.conf > /dev/null
auth enable sharedsecret *
auth enable gsi_auth *
_EOF_

sudo systemctl start gfarm-pgsql
sudo systemctl start gfmd

# update gfarm2.conf
set $(cat ~/.nodelist)
MASTER=$1
SSLAVE=$2
ASLAVE=$3
cat <<_EOF_ | sudo tee -a $CONFDIR/gfarm2.conf > /dev/null
metadb_server_list $MASTER:601 $SSLAVE:601 $ASLAVE:601
auth enable sharedsecret *
auth enable gsi_auth *
_EOF_
cp $CONFDIR/gfarm2.conf ~/local/

# shared keys for users
gfkey -f -p 31536000
KEY=.gfarm_shared_key
gfarm-pcp -p ~/$KEY .

# shared keys for system users
HOST=$(hostname)
HOSTS=$(grep -v $HOST ~/.nodelist)
for u in _gfarmmd _gfarmfs; do
	sudo -u $u gfkey -f -p 31536000
	for h in $HOSTS; do
		sudo cat /home/$u/$KEY | \
			ssh $h sudo -u $u tee /home/$u/$KEY > /dev/null
		ssh $h sudo -u $u chmod 600 /home/$u/$KEY
	done
done

# slave metadata servers
gfmdhost -m $MASTER -C siteA
gfmdhost -c $SSLAVE -C siteA
gfmdhost -c $ASLAVE -C siteB
sleep 3
sudo gfdump.postgresql -d -f d

for h in $SSLAVE $ASLAVE
do
	ssh $h sudo config-gfarm -N $CONFIG_OPTIONS
	cat <<_EOF_ | ssh $h sudo tee -a $CONFDIR/gfmd.conf > /dev/null
auth enable sharedsecret *
auth enable gsi_auth *
_EOF_
	scp -p d $h:
	ssh $h sudo systemctl start gfarm-pgsql
	ssh $h sudo gfdump.postgresql -r -f d
	ssh $h rm d

	ssh $h sudo cp local/gfarm2.conf $CONFDIR
done
sudo rm d

# gfsd
set $(cat ~/.nodelist)
shift
HOSTS="$*"
for h in $HOSTS
do
	ssh $h sudo cp local/gfarm2.conf $CONFDIR

	ssh $h sudo config-gfsd
	gfhost -c -a linux -p 600 -n $(nproc) $h
	ssh $h sudo systemctl start gfsd
done

status=0
echo Done
