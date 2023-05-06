#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; rm -f $TMPF ~/local/gfarm2.conf;
	exit $status' 0 1 2 15

TMPF=/tmp/$(basename $0)-$$
hostfile=$1
[ X"$hostfile" = X- ] && {
	hostfile=$TMPF
	cat > $hostfile
} || [ -f $hostfile ]
[ $# -gt 1 ] && init=true || init=false

# master metadata server
: ${USER:=$(id -un)}
grid-proxy-init
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
set $(cat $hostfile)
MASTER=$1 && ML="$MASTER:601"
if [ $# -gt 1 ]; then
	shift; SSLAVE=$1; ML="$ML $SSLAVE:601"; GL="$SSLAVE"
else
	SSLAVE=; GL="$MASTER"
fi
if [ $# -gt 1 ]; then
	shift; ASLAVE=$1; ML="$ML $ASLAVE:601"; GL="$GL $ASLAVE"
else
	ASLAVE=
fi
if [ $# -gt 1 ]; then
	shift; GL="$GL $*"
fi
cat <<_EOF_ | sudo tee -a $CONFDIR/gfarm2.conf > /dev/null
metadb_server_list $ML
auth enable sharedsecret *
auth enable gsi_auth *
_EOF_
cp $CONFDIR/gfarm2.conf ~/local/

# shared keys for users
if $init; then
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
fi

# slave metadata servers
gfmdhost -m $MASTER -C siteA
if [ X"$SSLAVE" != X ]; then
gfmdhost -c $SSLAVE -C siteA
[ X"$ASLAVE" = X ] || gfmdhost -c $ASLAVE -C siteB
sleep 3
sudo gfdump.postgresql -d -f d

for h in $SSLAVE $ASLAVE
do
	[ X"$h" = X ] && continue

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
fi

# gfsd
for h in $GL
do
	ssh $h sudo cp local/gfarm2.conf $CONFDIR

	ssh $h sudo config-gfsd
	gfhost -c -a linux -p 600 -n $(nproc) $h
	ssh $h sudo systemctl start gfsd
done

status=0
echo Done
