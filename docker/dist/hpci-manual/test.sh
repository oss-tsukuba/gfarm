#!/bin/sh

set -eu

HPCIID=$1
read PASS

python3 -m venv venv
. venv/bin/activate
pip install --upgrade pip
pip install orjson

echo $PASS | jwt-logon -s https://elpis.hpci.nii.ac.jp/ -l $HPCIID
sleep 3
jwt-parse

echo $PASS | nohup jwt-agent -s https://elpis.hpci.nii.ac.jp/ -l $HPCIID
sleep 3
jwt-parse

gfstatus
gfhost -lvu
mount.hpci
umount.hpci
