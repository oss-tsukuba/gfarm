#!/bin/sh

set -eu

HPCIID=$1
read PASS

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
