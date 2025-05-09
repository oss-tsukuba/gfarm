#!/bin/sh
${DEBUG:=false} && set -x
set -eu
status=1
trap '[ $status = 1 ] && echo NG; rm -f get_gfarm2conf.sh; exit $status' \
	0 1 2 15

# delete proxy cert
grid-proxy-destroy 2>/dev/null || :

sudo mkdir -p /etc/grid-security/certificates
OPWD=$PWD
cd /etc/grid-security/certificates
# HPCI SS CA
CERT=21d9c8b3.0
[ -f $CERT ] || {
	[ -f $OPWD/hpci/$CERT ] &&
		sudo cp $OPWD/hpci/$CERT . ||
		sudo wget https://www.hpci-office.jp/info/download/attachments/425328655/$CERT
}
cd $OPWD

[ -f get_gfarm2conf.sh ] ||
	wget https://www.hpci-office.jp/info/download/attachments/69471402/\
get_gfarm2conf.sh

[ -f ~/.gfarm2rc.hpci ] ||
	sh ./get_gfarm2conf.sh -f ~/.gfarm2rc.hpci

# install hpcissh
sudo apt-get -y install sshpass
PKG=hpcissh-clients; export PKG
[ -d ~/gfarm/$PKG ] ||
        (cd ~/gfarm && git clone https://github.com/hpci-auth/$PKG.git)
(cd ~/gfarm/$PKG && sudo sh ./install.sh > /dev/null)

echo \# YOU NEED TO DO THE FOLLOWING
echo export GFARM_CONFIG_FILE=$HOME/.gfarm2rc.hpci
echo jwt-agent -s https://elpis.hpci.nii.ac.jp/ -l HPCI_ID
status=0
echo Done
