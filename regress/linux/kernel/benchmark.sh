#!/bin/sh

MOUNTPOINT=/mnt/gfarm
FILE=benchfile
IOSIZE=100
BENCH=src/benchmark
LOOP=3
USER=gfarm

mountGfarm() {
	/etc/init.d/gfsk start
	mount -t gfarm -o conf_path=/usr/local/etc/gfarm2.conf,luser=gfarm /dev/gfarm ${MOUNTPOINT}
}

umountGfarm() {
	umount ${MOUNTPOINT}
	/etc/init.d/gfsk stop
}

remountGfarm() {
	umountGfarm
	mountGfarm
	sleep 1
}

rwbench() {
	i=1
	while [ $i -le $LOOP ]; do
		sudo -u ${USER} ${BENCH} ${MOUNTPOINT} ${FILE} ${IOSIZE}
		sleep 1
		i=`expr $i + 1`
	done
}

readbench() {
	i=1
	while [ $i -le $LOOP ]; do
		remountGfarm
		sudo -u ${USER} ${BENCH} ${MOUNTPOINT} ${FILE} ${IOSIZE} read
		sleep 1
		i=`expr $i + 1`
	done
}

user=`whoami`
if [ "$user" != "root" ]; then
	echo "This test script must run by root"
	exit 1
fi

if [ ! -f ${BENCH} ]; then
	echo "Cannot find ${BENCH}"
	exit 1
fi


mountGfarm

rwbench
readbench

sudo -u ${USER} rm -f ${MOUNTPOINT}/${FILE}

umountGfarm

