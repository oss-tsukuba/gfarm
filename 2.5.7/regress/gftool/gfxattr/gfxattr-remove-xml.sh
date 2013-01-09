# xml xattr remove test - 1
{
	gfxattr -rx / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr remove test - 2
{
	gfxattr -rx / $attrname2 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr remove test - 3
{
	gfxattr -rx / "$attrname"XXX 
	if [ $? == 0 ]; then
		# must fail if not exists
		exit $exit_fail
	fi
}

# xml xattr remove test - 4
{
	gfxattr -rx $fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr remove test - 5
{
	gfxattr -rx $fileX $attrname2 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr remove test - 6
{
	gfxattr -rx $fileX "$attrname"XXX 
	if [ $? == 0 ]; then
		# must fail if not exists
		exit $exit_fail
	fi
}

echo -n ""> $nameslist

# xml xattr list test - 5
{
	gfxattr -lx / > $getnames 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr list test - 6
{
	gfxattr -lx $fileX > $getnames 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}
