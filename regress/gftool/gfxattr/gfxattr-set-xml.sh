# xml xattr set test - 1
{
	echo '<a>Xml1</a>' > $attrfile
	gfxattr -sx -f $attrfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -gx -f $getfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr set test - 2
{
	echo '<a>Xml2</a>' > $attrfile2
	gfxattr -sx -c -f $attrfile2 / $attrname 
	if [ $? == 0 ]; then
		# must fail if already exists 
		exit $exit_fail
	fi
	gfxattr -gx -f $getfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr set test - 3
{
	echo '<a>Xml3</a>' > $attrfile
	gfxattr -sx -m -f $attrfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -gx -f $getfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr set test - 4
{
	echo '<a>Xml4</a>' > $attrfile
	gfxattr -sx -m -f $attrfile / "$attrname"4 
	if [ $? == 0 ]; then
		# must fail if not exists 
		exit $exit_fail
	fi
}

# xml xattr set test - 5
{
	echo '<a>Xml5</a>' > $attrfile
	gfxattr -sx -f $attrfile $subdir $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -gx -f $getfile $subdir $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr set test - 6
{
	echo '<a>Xml6</a>' > $attrfile
	gfxattr -sx -f $attrfile $subsubdir $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -gx -f $getfile $subsubdir $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr set test - 7
{
	echo '<a>Xml7</a>' > $attrfile
	gfxattr -sx -f $attrfile $fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -gx -f $getfile $fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr set test - 8
{
	echo '<a>Xml8</a>' > $attrfile
	gfxattr -sx -f $attrfile $subdir/$fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -gx -f $getfile $subdir/$fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr set test - 8
{
	echo '<a>Xml9</a>' > $attrfile
	gfxattr -sx -f $attrfile $subsubdir/$fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -gx -f $getfile $subsubdir/$fileX $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}
