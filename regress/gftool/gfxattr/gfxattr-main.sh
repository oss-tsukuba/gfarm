. ./regress.conf
. ./gftool/gfxattr/gfxattr-init.sh

trap 'exit $exit_trap' $trap_sigs

echo "*** start gfxattr test. some tests issue error messages. ***"

mkdir -p $fusemnt
if [ $? != 0 ]; then
	exit $exit_fail
fi
gfarm2fs $fusemnt
if [ $? != 0 ]; then
	exit $exit_fail
fi

mkdir -p $xattrtmp
gfmkdir $subdir
gfmkdir $subdir2
gfmkdir $subsubdir
touch $fusemnt/$fileX
touch $fusemnt/$subdir/$fileX
touch $fusemnt/$subsubdir/$fileX

echo "** set/get normal test **"
. ./gftool/gfxattr/gfxattr-set-normal.sh

echo "** list normat test **"
. ./gftool/gfxattr/gfxattr-list-normal.sh

echo "** remove normat test **"
. ./gftool/gfxattr/gfxattr-remove-normal.sh

echo "** limit normat test **"
. ./gftool/gfxattr/gfxattr-limit-normal.sh

echo "** stat normal test **"
. ./gftool/gfxattr/gfxattr-stat-normal.sh

echo "** permission normal test **"
. ./gftool/gfxattr/gfxattr-perm-normal.sh

echo "** fuse test **"
. ./gftool/gfxattr/gfxattr-fuse.sh


if [ $xml_enabled == 1 ]; then
	echo "** set/get xml test **"
	. ./gftool/gfxattr/gfxattr-set-xml.sh
	
	echo "** list xml test **"
	. ./gftool/gfxattr/gfxattr-list-xml.sh
	
	echo "** remove xml test **"
	. ./gftool/gfxattr/gfxattr-remove-xml.sh

	echo "** stat xml test **"
	. ./gftool/gfxattr/gfxattr-stat-xml.sh

	echo "** permission xml test **"
	. ./gftool/gfxattr/gfxattr-perm-xml.sh

	echo "** enabled xml common test **"
	. ./gftool/gfxattr/gfxattr-xml-enabled-common.sh

	echo "** find xml attr test **"
	. ./gftool/gfxattr/gfxattr-findxmlattr.sh
else
	echo "** disabled xml common test **"
	. ./gftool/gfxattr/gfxattr-xml-disabled-common.sh
fi


. ./gftool/gfxattr/gfxattr-fini.sh

echo "*** gfxattr test passed! ***"

exit $exit_pass
