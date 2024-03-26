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
esac

CONF=true
install_option=all
while [ $# -gt 0 ]
do
	case $1 in
	-m) CONF=false ;;
	single) install_option=$1 ;;
	*) exit 1 ;;
	esac
	shift
done

if $CONF; then
	rm -rf build
	mkdir build
	cd build

	../configure $CONF_OPT
else
	cd build
fi
make -j $(nproc) > /dev/null
sudo make install > /dev/null

if [ $install_option = all ]; then
	# -p cannot be used because the following error happens
	# mv: cannot stat 'libgfsl_gsi.so.1.0.0': No such file or directory
	gfarm-prun "(cd $PWD; sudo make install > /dev/null)"
fi

status=0
echo Done
