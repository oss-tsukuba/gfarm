#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; exit $status' 0 1 2 15

: ${PKG:=gfarm}
: ${VER:=}

# wget https://github.com/oss-tsukuba/gfarm/archive/refs/tags/$VER.tar.gz
# or
# git clone https://github.com/oss-tsukuba/gfarm.git gfarm-$VER

[ X"$VER" = X ] && {
	for f in $PKG-*.tar.gz $PKG-*
	do
		[ -e $f ] || continue

		case $f in
		$PKG-*.tar.gz)
			VER=$(echo $f | sed "s/$PKG-\(.*\).tar.gz/\1/") ;;
		*)
			VER=$(echo $f | sed "s/$PKG-\(.*\)/\1/") ;;
		esac
		break
	done
}

NEEDTAR=false
COPY=false
if [ -f $PKG-$VER.tar.gz ]; then
	[ -d $PKG-$VER ] && exit 1

	tar zxfp $PKG-$VER.tar.gz --wildcards *$PKG.spec
	SPEC=$(find $PKG-$VER -type f -name $PKG.spec || :)
	[ X"$SPEC" = X ] && exit 1

elif [ -d $PKG-$VER ]; then
	SPEC=$(find $PKG-$VER -type f -name $PKG.spec || :)
	[ X"$SPEC" = X ] && exit 1
	NEEDTAR=true

elif [ -d $PKG ]; then
	SPEC=$(find $PKG -type f -name $PKG.spec || :)
	[ X"$SPEC" = X ] && exit 1
	NEEDTAR=true

	VER=$(awk '/%define ver/{print $3;done=1}
		   /Version:/{if (done!=1) print $2}' $SPEC)
	[ X"$VER" = X ] && exit 1

	cp -a $PKG $PKG-$VER > /dev/null 2>&1 || :
	COPY=true
else
	echo "no package for $PKG"
	exit 1
fi
if $NEEDTAR; then
	[ -f $PKG-$VER/Makefile ] &&
		(cd $PKG-$VER && make distclean > /dev/null)

	tar --exclude-ignore-recursive=$PKG-$VER/.gitignore \
		--exclude=.git -zcf $PKG-$VER.tar.gz $PKG-$VER \
		> /dev/null 2>&1 ||
		tar --exclude=.git -zcf $PKG-$VER.tar.gz $PKG-$VER
fi

mkdir -p ~/rpmbuild/SOURCES
mv $PKG-$VER.tar.gz ~/rpmbuild/SOURCES/
rpmbuild -bs --undefine dist $SPEC
[ $COPY ] && sudo rm -rf $PKG-$VER > /dev/null 2>&1 || :

#export GFARM_CONFIGURE_OPTION="--with-globus --with-infiniband"
export GFARM_CONFIGURE_OPTION="--with-globus"
rpmbuild --rebuild ~/rpmbuild/SRPMS/$PKG-$VER-1.src.rpm > /dev/null
status=0
echo Done
