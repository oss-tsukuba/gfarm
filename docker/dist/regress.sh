#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; \
	gfrm -f $TFILE; exit $status' 0 1 2 15

TFILE=/tmp/corrupted-file
ENV="LANG=C GFARM_TEST_MDS2=c6:601 GFARM_TEST_MDS3=c7:601 \
	GFARM_TEST_MDS4=c8:601 GFARM_TEST_CKSUM_MISMATCH=$TFILE"
export $ENV

DISTDIR=$PWD

grid-proxy-init -q || :

gfmkdir -p /tmp
gfchmod 1777 /tmp || :

cd ~/gfarm/regress
make all > /dev/null

create_mismatch_file()
{
	FILE1=server/gfmd/.libs/gfmd
	gfreg -h c2 ../$FILE1 $TFILE
	gfrep -qD c3 $TFILE
	for h in c2 c3; do
		echo -n XXX | ssh $h sudo dd conv=notrunc \
			of=/var/gfarm-spool/$(gfspoolpath $TFILE)
	done
}

create_gfmd_restart_all()
{
	cat <<EOF > bin/gfmd_restart_all
#!/bin/sh
gfmdhost | gfarm-prun -a -p -h - sudo systemctl restart gfmd
EOF
	chmod +x bin/gfmd_restart_all
}

update_gfarm2rc()
{
	[ -f ~/.gfarm2rc ] || touch ~/.gfarm2rc
	cp -p ~/.gfarm2rc ~/.gfarm2rc.bak
	awk '/^client_digest_check/ { next } \
	     { print } \
	     END { print "client_digest_check enable" }' ~/.gfarm2rc.bak \
	     > ~/.gfarm2rc
}

create_gfmd_restart_all

update_gfarm2rc
scp -p ~/.gfarm2rc c2:

AUTH=$(gfhost -lv | head -1 | awk '{ print $2 }')
DIST=$(grep ^ID= /etc/os-release | sed 's/ID="*\([a-z]*\)"*/\1/')
DATE=$(date +%F-%T)
LOG1=log.rm-root-$AUTH-$DIST-$DATE
LOG2=log.lc-user-$AUTH-$DIST-$DATE

create_mismatch_file
gfsudo ./regress.sh -l $LOG1

create_mismatch_file
ssh c2 "(grid-proxy-init -q; cd gfarm/regress && $ENV ./regress.sh -l $LOG2)"

./addup.sh $LOG1 $LOG2 | egrep '(UNSUPPORTED|FAIL)'

status=0
