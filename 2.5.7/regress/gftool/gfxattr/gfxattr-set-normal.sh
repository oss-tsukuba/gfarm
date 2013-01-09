# normal xattr set test - 1
{
	echo Hello1 > $attrfile
	gfxattr -s -f $attrfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -g -f $getfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr set test - 2
{
	echo Hello2 > $attrfile2
	gfxattr -s -c -f $attrfile2 / $attrname 
	if [ $? == 0 ]; then
		# must fail if already exists 
		exit $exit_fail
	fi
	gfxattr -g -f $getfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr set test - 3
{
	echo Hello3 > $attrfile
	gfxattr -s -m -f $attrfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -g -f $getfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr set test - 4
{
	echo Hello3 > $attrfile
	gfxattr -s -m -f $attrfile / "$attrname"4
	if [ $? == 0 ]; then
		# must fail if not exists 
		exit $exit_fail
	fi
}

# normal xattr set test - 5
{
	echo Hello5 > $attrfile
	gfxattr -s -f $attrfile $subdir $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -g -f $getfile $subdir $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr set test - 6
{
	echo Hello6 > $attrfile
	gfxattr -s -f $attrfile $subsubdir $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -g -f $getfile $subsubdir $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr set test - 7
{
	echo Hello7 > $attrfile
	gfxattr -s -f $attrfile $fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -g -f $getfile $fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr set test - 8
{
	echo Hello8 > $attrfile
	gfxattr -s -f $attrfile $subdir/$fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -g -f $getfile $subdir/$fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr set test - 9
{
	echo Hello9 > $attrfile
	gfxattr -s -f $attrfile $subsubdir/$fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -g -f $getfile $subsubdir/$fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}
