#!/bin/sh

set -eu

HPCIID=$1
read PASS

python3 -m venv venv
. venv/bin/activate
pip install --upgrade pip
pip install orjson docopt schema

echo $PASS | jwt-logon -s https://elpis.hpci.nii.ac.jp/ -l $HPCIID
sleep 3
jwt-parse

echo $PASS | nohup jwt-agent -s https://elpis.hpci.nii.ac.jp/ -l $HPCIID
sleep 3
jwt-parse

set -x

gfstatus
gfhost -lvu
OUTD=test-$HPCIID-$$
gfptar -c gfarm:/tmp/$OUTD-gz -C / hpci-manual
gfptar -c gfarm:/tmp/$OUTD-bz2 -T bz2 -C / hpci-manual
gfptar -c gfarm:/tmp/$OUTD-xz -T xz -C / hpci-manual
gfrm -rf gfarm:/tmp/$OUTD-gz gfarm:/tmp/$OUTD-bz2 gfarm:/tmp/$OUTD-xz
mount.hpci
umount.hpci
