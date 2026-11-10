#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; exit $status' 0 1 2 15

: ${PKG:=gfarm}

CONF_OPT=
case $PKG in
gfarm)
	CONF_OPT="--enable-xmlattr --with-globus" #--with-infiniband
	if grep "CentOS Linux release 7" /etc/system-release > /dev/null
	then
		CONF_OPT="$CONF_OPT --with-openssl=openssl11"
	fi
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
MAKE_CLEAN=false
while [ $# -gt 0 ]
do
	case $1 in
	asan) OPTFLAGS='-g -Og -Wall -fsanitize=address,undefined -fsanitize-recover=all -fno-omit-frame-pointer -fno-common';;
	tsan) OPTFLAGS='-g -Og -Wall -fsanitize=thread -fsanitize-recover=all -fno-omit-frame-pointer -fno-common';;
	single) install_option=$1 ;;
	build_only) BUILD_ONLY=true ;;
	clean) MAKE_CLEAN=true ;;
	*) exit 1 ;;
	esac
	shift
done

ARCH_GUESS=gftool/config-gfarm/gfarm.arch.guess
[ -f $ARCH_GUESS ] || ARCH_GUESS=gfarm.arch.guess
BUILDDIR=build-$($ARCH_GUESS)

if $MAKE_CLEAN; then
	rm -rf $BUILDDIR
	# configure for the VPATH build below fails with "source directory
	# already configured" when the source directory has been configured
	# by an in-tree build
	if [ -f config.status ]; then
		make distclean > /dev/null
	fi
fi
# a build directory without Makefile is left when configure failed.
# remove it to run configure again, otherwise make fails
if [ -d $BUILDDIR ] && [ ! -f $BUILDDIR/Makefile ]; then
	rm -rf $BUILDDIR
fi
if [ -d $BUILDDIR ]; then
	cd $BUILDDIR
else
	mkdir $BUILDDIR
	cd $BUILDDIR

	# pass the sanitizer options to configure as well.
	# for gfarm, CFLAGS is recorded as the default of OPTFLAGS, thus
	# the programs built later by "make" (e.g. regress) use them too.
	# for GNU automake packages, otherwise the link tests against
	# the sanitized libgfarm fail, and the functions are regarded as
	# not available.
	../configure $CONF_OPT ${OPTFLAGS:+"CFLAGS=${OPTFLAGS}"}
fi

if [ $PKG = gfarm ]; then
	make ${OPTFLAGS:+"OPTFLAGS=${OPTFLAGS}"} -j $(nproc) > /dev/null
else # assume GNU automake
	make ${OPTFLAGS:+"CFLAGS=${OPTFLAGS}"} -j $(nproc) > /dev/null
fi

if $BUILD_ONLY; then
	status=0
	echo Done
	exit 0
fi

if [ $install_option = all ] && [ $PKG = gfarm ]; then
	# "make install" of gfarm cannot be executed on hosts in parallel,
	# since libtool relinks libraries in the shared build directory
	# (mv: cannot stat 'libgfsl_gsi.so.1.0.0': No such file or directory).
	# install it once to a staging directory, and extract the archive
	# on all hosts in parallel via the volume shared by all hosts.
	STAGE=$HOME/local/install-stage
	ARCHIVE=$HOME/local/install-stage.tar
	sudo rm -rf $STAGE $ARCHIVE
	sudo make DESTDIR=$STAGE install > /dev/null
	sudo tar -C $STAGE -cf $ARCHIVE .
	# --no-overwrite-dir: keep the metadata of existing directories.
	# ldconfig: libtool does not update the cache of the shared libraries
	# when DESTDIR is specified.
	# extract it on this host first, since gfarm-prun is installed by it
	EXTRACT="sudo tar -C / --no-overwrite-dir -xf $ARCHIVE && sudo ldconfig"
	sh -c "$EXTRACT"
	gfarm-prun -p "$EXTRACT"
	sudo rm -rf $STAGE $ARCHIVE
else
	sudo make install > /dev/null
	if [ $install_option = all ]; then
		gfarm-prun -p "(cd $PWD; sudo make install > /dev/null)"
	fi
fi

status=0
echo Done
