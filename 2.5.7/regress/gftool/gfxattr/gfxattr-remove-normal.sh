# normal xattr remove test - 1
{
	gfxattr -r / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr remove test - 2
{
	gfxattr -r / $attrname2 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr remove test - 3
{
	gfxattr -r / "$attrname"XXX 
	if [ $? == 0 ]; then
		# must fail if not exists
		exit $exit_fail
	fi
}

# normal xattr remove test - 4
{
	gfxattr -r $fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr remove test - 5
{
	gfxattr -r $fileX $attrname2 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr remove test - 6
{
	gfxattr -r $fileX "$attrname"XXX 
	if [ $? == 0 ]; then
		# must fail if not exists
		exit $exit_fail
	fi
}

echo -n ""> $nameslist

# normal xattr list test - 5
{
	gfxattr -l / > $getnames 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr list test - 6
{
	gfxattr -l $fileX > $getnames 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}
