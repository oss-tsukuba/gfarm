#!/bin/sh

usage() {
	echo >&2 "Usage: sh batchtest.sh HPCI-ID"
	exit 2
}

[ $# -eq 1 ] || usage
case $1 in
	hpci*) HPCIID=$1 ;;
	*) usage ;;
esac

if [ -t 0 ]; then
	echo -n "Passphrase: "
	trap "stty echo" HUP INT QUIT TERM
	stty -echo
	read PASS
	stty echo
	echo
else
	read PASS
fi

for d in centos7 rockylinux9 ubuntu20 ubuntu22 ubuntu24
do
	[ -d $d ] || continue
	echo $d
	(cd $d && make build)
       	echo $PASS | docker run -i --device /dev/fuse --privileged --rm \
		-u ${USER} -w /home/${USER} -v .:/hpci-manual hpci-$d \
		sh /hpci-manual/test.sh $HPCIID
done
