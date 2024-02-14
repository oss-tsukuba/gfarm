#!/bin/sh
set -xeu

PROJ=hp120273
HPCI_ID=hpci000543
GHOME=home/$PROJ/$HPCI_ID

#GFSD1=gfs74-2.hpci.itc.u-tokyo.ac.jp
#GFSD2=ss-10-1-3.r-ccs.riken.jp
GFSD1=$(gfsched -n 1 -D u-tokyo.ac.jp)
GFSD2=$(gfsched -n 1 -D r-ccs.riken.jp)
[ X"$GFSD1" != X ]
[ X"$GFSD2" != X ]

DATABASE=/tmp/database.db
MPOINT=/tmp/gfperf

# sh all.sh min
# sh hpci.sh 
# export GFARM_CONFIG_FILE=$HOME/.gfarm2rc.hpci
# mv ~/.globus ~/.globus.bak
# myproxy-logon -s portal.hpci.nii.ac.jp -t 168 -l $HPCI_ID

GFCONF=~/.gfarm2rc.hpci
[ -f $GFCONF ] || exit 1

for d in /etc /usr/local/etc
do
        [ -f $d/gfarm2.conf ] && break
done
[ -f $d/gfarm2.conf ] && SYSCONF=$d/gfarm2.conf || exit 1

echo "auth enable tls_client_certificate *" >> $GFCONF
awk '/^auth/ {print "auth disable", $3, $4}' $SYSCONF >> $GFCONF

grid-proxy-info
gfhost -lv $GFSD1 $GFSD2

sudo apt-get -y update
sudo apt-get -y install php php-sqlite3 ruby ruby-sqlite3 sqlite gnuplot

cat <<EOF > test.yml
database: {filename: "$DATABASE", check span: "10days",
           backup: "$DATABASE.bak" }
authentication: ["tls_client_certificate"]
gfarm2fs_mountpoint: ["$MPOINT"]
metadata: [ {testdir: "gfarm:///$GHOME", number: "250"},
            {testdir: "file://$MPOINT/$GHOME", number: "250"} ]
copy: [
   {src: "file:///tmp", dst: "gfarm:///$GHOME", filesize: "100M", bufsize: "4K", gfsd: "$GFSD1", gfarm2fs: "$MPOINT"},
   {src: "gfarm:///$GHOME", dst: "file:///tmp", filesize: "100M", bufsize: "4K", gfsd: "$GFSD1", gfarm2fs: "$MPOINT"},
   {src: "file:///tmp", dst: "gfarm:///$GHOME", filesize: "100M", bufsize: "4K", gfsd: "$GFSD2", gfarm2fs: "$MPOINT"},
   {src: "gfarm:///$GHOME", dst: "file:///tmp", filesize: "100M", bufsize: "4K", gfsd: "$GFSD2", gfarm2fs: "$MPOINT"},
      ]
EOF

mkdir -p $MPOINT
gfperf.rb test.yml 

# sudo apt-get -y install apache2
sudo mkdir /var/www/html/gfperf

cd ~/gfarm/bench/gfperf/gfperf-web/
sudo cp *.php /var/www/html/gfperf/

cd /var/www/html/gfperf/
sudo mv config.php config.php.bak
# sudo sh -c "sed 's|var/www|var/www/gfperf|' config.php.bak > config.php"
sudo sh -c "sed 's|var/www|tmp|' config.php.bak > config.php"

# vi /etc/apache2/sites-available/000-default.conf
# vi /etc/apache2/conf-available/gfperf.conf
# ln -s ../conf-available/gfperf.conf /etc/apache2/conf-enabled/

sudo service apache2 start 
