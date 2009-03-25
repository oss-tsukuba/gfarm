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

# xml xattr perm test - 1
{
	gfxattr -sx -f $attrfile $target $attrname 
	if [ $? == 0 ]; then
		# must fail
		exit $exit_fail
	fi
}

# xml xattr perm test - 2
{
	gfxattr -gx $target $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr perm test - 3
{
	gfxattr -lx $target 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr perm test - 4
{
	gfxattr -rx $target $attrname 
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

# xml xattr perm test - 5
{
	gfxattr -sx -f $attrfile $target $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr perm test - 6
{
	gfxattr -gx $target $attrname 
	if [ $? == 0 ]; then
		# must fail
		exit $exit_fail
	fi
}

# xml xattr perm test - 7
{
	gfxattr -lx $target 
	if [ $? == 0 ]; then
		# msut fail
		exit $exit_fail
	fi
}

# xml xattr perm test - 8
{
	gfxattr -rx $target $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

gfchmod 0755 $target
if [ $? != 0 ]; then
	exit $exit_fail
fi
