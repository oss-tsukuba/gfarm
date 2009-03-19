# normal xattr list test - 1
{
	echo $attrname > $nameslist
	gfxattr -l / > $getnames 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}


# normal xattr list test - 2
{
	echo $attrname2 >> $nameslist
	gfxattr -s -f $attrfile / $attrname2 
	gfxattr -l / > $getnames 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr list test - 3
{
	echo $attrname > $nameslist
	gfxattr -l $fileX > $getnames 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr list test - 4
{
	echo $attrname2 >> $nameslist
	gfxattr -s -f $attrfile $fileX $attrname2 
	gfxattr -l $fileX > $getnames 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}
