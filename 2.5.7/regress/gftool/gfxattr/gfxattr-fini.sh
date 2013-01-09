. ./gftool/gfxattr/gfxattr-init.sh

gfxattr -r / $attrname
gfxattr -r / $attrname2
gfxattr -rx / $attrname
gfxattr -rx / $attrname2

rm -rf $xattrtmp
gfchmod 0755 $subdir
rm -rf $fusemnt/$subdir
rm -rf $fusemnt/$subdir2
rm -f $fusemnt/$fileX
sudo umount $fusemnt
