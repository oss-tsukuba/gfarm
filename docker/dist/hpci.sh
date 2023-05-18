#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; rm -f get_gfarm2conf.sh; exit $status' \
	0 1 2 15

GSICONF=/etc/grid-security/gsi.conf
grep "NAME_COMPATIBILITY=HYBRID" $GSICONF || {
	sudo mv $GSICONF $GSICONF.orig
	awk '/^NAME_COMPATIBILITY=/{print "NAME_COMPATIBILITY=HYBRID";next}
	     {print}' $GSICONF.orig | sudo tee $GSICONF > /dev/null
}

sudo mkdir -p /etc/grid-security/certificates
OPWD=$PWD
cd /etc/grid-security/certificates
[ -f 61cd35bd.signing_policy ] ||
	sudo wget https://www.hpci.nii.ac.jp/ca/61cd35bd.signing_policy
[ -f 61cd35bd.0 ] ||
	sudo wget https://www.hpci.nii.ac.jp/ca/61cd35bd.0
cd $OPWD

[ -f get_gfarm2conf.sh ] ||
	wget https://www.hpci-office.jp/info/download/attachments/69471402/\
get_gfarm2conf.sh

[ -f ~/.gfarm2rc.hpci ] ||
	sh ./get_gfarm2conf.sh -f ~/.gfarm2rc.hpci


echo mv ~/.globus ~/.globus.bak
echo myproxy-logon -s portal.hpci.nii.ac.jp -t 168 -l HPCI-ID
echo export GFARM_CONFIG_FILE=$HOME/.gfarm2rc.hpci
status=0
echo Done