#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; \
	gfrm -f $TFILE; exit $status' 0 1 2 15

: ${ASAN_OPTIONS=halt_on_error=false,log_exe_name=true,log_path=/var/tmp/gfarm.log.asan}
: ${LSAN_OPTIONS=halt_on_error=false,log_exe_name=true,log_path=/var/tmp/gfarm.log.lsan}
: ${UBSAN_OPTIONS=halt_on_error=false,log_exe_name=true,log_path=/var/tmp/gfarm.log.ubsan}
: ${TSAN_OPTIONS=halt_on_error=false,log_exe_name=true,log_path=/var/tmp/gfarm.log.tsan}
export ASAN_OPTIONS LSAN_OPTIONS UBSAN_OPTIONS TSAN_OPTIONS

TFILE=/tmp/corrupted-file
ENV="GFARM_TEST_MDS2=c6:601 GFARM_TEST_MDS3=c7:601 \
	GFARM_TEST_MDS4=c8:601 GFARM_TEST_CKSUM_MISMATCH=$TFILE \
	${ASAN_OPTIONS:+ASAN_OPTIONS=${ASAN_OPTIONS}} \
	${LSAN_OPTIONS:+LSAN_OPTIONS=${LSAN_OPTIONS}} \
	${UBSAN_OPTIONS:+UBSAN_OPTIONS=${UBSAN_OPTIONS}} \
	${TSAN_OPTIONS:+TSAN_OPTIONS=${TSAN_OPTIONS}}"
export $ENV

DISTDIR=$PWD

is_asan_enabled()
{
	ASAN_OPTIONS=help=true,halt_on_error=false gfstatus -V 2>&1 | sed q |
		grep AddressSanitizer >/dev/null
}

is_tsan_enabled()
{
	TSAN_OPTIONS=help=true,halt_on_error=false gfstatus -V 2>&1 | sed q |
		grep ThreadSanitizer >/dev/null
}

if is_asan_enabled; then
	OPTFLAGS='-g -Og -Wall -fsanitize=address,undefined -fsanitize-recover=all -fno-omit-frame-pointer -fno-common'
elif is_tsan_enabled; then
	OPTFLAGS='-g -Og -Wall -fsanitize=thread -fsanitize-recover=all -fno-omit-frame-pointer -fno-common'
fi

grid-proxy-init -q || :

gfmkdir -p /tmp
gfchmod 1777 /tmp || :

TOP=~/gfarm
BUILD=$TOP/build-$(gfarm.arch.guess)
MAKE=$TOP/makes/make.sh
cd $BUILD/regress
$MAKE ${OPTFLAGS:+"OPTFLAGS=${OPTFLAGS}"} all > /dev/null

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
	mkdir -p bin
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
DATE=$(date +%F.%H_%M_%S)
LOG1=log.rm-root.$AUTH.$DIST.$DATE
LOG2=log.lc-user.$AUTH.$DIST.$DATE

create_mismatch_file
gfsudo $MAKE REGRESS_ARGS="-l $LOG1" check

create_mismatch_file
C2DIST=$(ssh c2 gfarm.arch.guess)
C2DIR=~/gfarm/build-$C2DIST/regress
ssh c2 "(grid-proxy-init -q; cd $C2DIR &&
	$ENV $MAKE REGRESS_ARGS='-l $LOG2' check)"

$TOP/regress/addup.sh $LOG1 $C2DIR/$LOG2 | egrep '(UNSUPPORTED|FAIL)'

status=0
