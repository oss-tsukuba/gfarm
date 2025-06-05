#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; exit $status' 0 1 2 15

: ${PKG:=gfarm}

CONF_OPT=
PRUN_ARG=-p
case $PKG in
gfarm)
	CONF_OPT="--enable-xmlattr --with-globus" #--with-infiniband
	if grep "CentOS Linux release 7" /etc/system-release > /dev/null
	then
		CONF_OPT="$CONF_OPT --with-openssl=openssl11"
	fi
	PRUN_ARG=
	;;
gfarm2fs)
	CONF_OPT=--with-gfarm=/usr/local
	;;
cyrus-sasl-xoauth2-idp)
	CONF_OPT=--libdir=$(pkg-config --variable=libdir libsasl2)
	;;
esac

BUILD_ONLY=false
install_option=all
while [ $# -gt 0 ]
do
	case $1 in
	single) install_option=$1 ;;
	build_only) BUILD_ONLY=true ;;
	*) exit 1 ;;
	esac
	shift
done

ARCH_GUESS=gftool/config-gfarm/gfarm.arch.guess
[ -f $ARCH_GUESS ] || ARCH_GUESS=gfarm.arch.guess
BUILDDIR=build-$($ARCH_GUESS)

if [ -d $BUILDDIR ]; then
	cd $BUILDDIR
else
	mkdir $BUILDDIR
	cd $BUILDDIR

	../configure $CONF_OPT
fi

make -j $(nproc) > /dev/null

if $BUILD_ONLY; then
	status=0
	echo Done
	exit 0
fi

sudo make install > /dev/null

if [ $install_option = all ]; then
	OPT=-p
	# -p cannot be used because the following error happens
	# mv: cannot stat 'libgfsl_gsi.so.1.0.0': No such file or directory
	[ $PKG = gfarm ] && OPT=
	gfarm-prun $OPT $PRUN_ARG "(cd $PWD; sudo make install > /dev/null)"
fi

status=0
echo Done
