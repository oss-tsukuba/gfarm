#!/bin/sh
set -xeu

# master metadata server
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
cat <<_EOF_ | sudo tee -a $CONFDIR/gfarm2.conf > /dev/null
metadb_server_list c1:601 c2:601 c3:601
auth enable sharedsecret *
auth enable gsi_auth *
_EOF_
cp $CONFDIR/gfarm2.conf ~/local/

# shared keys for users
gfkey -f -p 31536000
KEY=.gfarm_shared_key
for h in c2 c3 c4
do
	scp -p ~/$KEY $h:
done

# shared keys for system users
for u in _gfarmmd _gfarmfs; do
	sudo -u $u gfkey -f -p 31536000
	sudo -u $u gfkey -f -p 31536000
	for h in c2 c3 c4; do
		sudo cat /home/$u/$KEY | \
			ssh $h sudo -u $u tee /home/$u/$KEY > /dev/null
		ssh $h sudo -u $u chmod 600 /home/$u/$KEY
	done
done

# slave metadata servers
gfmdhost -c c2
gfmdhost -c c3 -C siteA
sleep 3
sudo gfdump.postgresql -d -f d

for h in c2 c3
do
	ssh $h sudo config-gfarm -N $CONFIG_OPTIONS
	scp -p d $h:
	ssh $h sudo systemctl start gfarm-pgsql
	ssh $h sudo gfdump.postgresql -r -f d
	ssh $h rm d
	cat <<_EOF_ | ssh $h sudo tee -a $CONFDIR/gfmd.conf > /dev/null
auth enable sharedsecret *
auth enable gsi_auth *
_EOF_
	ssh $h sudo systemctl start gfmd

	ssh $h sudo cp local/gfarm2.conf $CONFDIR
done
sudo rm d

# gfsd
for h in c2 c3 c4
do
	ssh $h sudo cp local/gfarm2.conf $CONFDIR

	ssh $h sudo config-gfsd
	gfhost -c -a linux -p 600 -n 8 $h
	ssh $h sudo systemctl start gfsd
done

rm ~/local/gfarm2.conf
