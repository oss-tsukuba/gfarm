target="$subdir"
echo 'Hello1' > $attrfile
gfxattr -s -f $attrfile $target $attrname 
if [ $? != 0 ]; then
	exit $exit_fail
fi

#
# drop owner write permission
#
gfchmod 0555 $target
if [ $? != 0 ]; then
	exit $exit_fail
fi

# normal xattr perm test - 1
{
	gfxattr -s -f $attrfile $target $attrname 
	if [ $? == 0 ]; then
		# must fail
		exit $exit_fail
	fi
}

# normal xattr perm test - 2
{
	gfxattr -g $target $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr perm test - 3
{
	gfxattr -l $target 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr perm test - 4
{
	gfxattr -r $target $attrname 
	if [ $? == 0 ]; then
		# must fail
		exit $exit_fail
	fi
}

#
# drop owner read permission
#
gfchmod 0355 $target
if [ $? != 0 ]; then
	exit $exit_fail
fi

# normal xattr perm test - 5
{
	gfxattr -s -f $attrfile $target $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr perm test - 6
{
	gfxattr -g $target $attrname 
	if [ $? == 0 ]; then
		# must fail
		exit $exit_fail
	fi
}

# normal xattr perm test - 7
{
	gfxattr -l $target 
	if [ $? == 0 ]; then
		# msut fail
		exit $exit_fail
	fi
}

# normal xattr perm test - 8
{
	gfxattr -r $target $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

gfchmod 0755 $target
if [ $? != 0 ]; then
	exit $exit_fail
fi
