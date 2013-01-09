# xml xattr list test - 1
{
	echo $attrname > $nameslist
	gfxattr -lx / > $getnames 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}


# xml xattr list test - 2
{
	echo $attrname2 >> $nameslist
	gfxattr -sx -f $attrfile / $attrname2 
	gfxattr -lx / > $getnames 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr list test - 3
{
	echo $attrname > $nameslist
	gfxattr -lx $fileX > $getnames 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr list test - 4
{
	echo $attrname2 >> $nameslist
	gfxattr -sx -f $attrfile $fileX $attrname2 
	gfxattr -lx $fileX > $getnames 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}
