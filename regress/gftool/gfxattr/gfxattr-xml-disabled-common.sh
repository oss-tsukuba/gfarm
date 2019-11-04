# xml xattr test - 1
{
	gfxattr -sx -f $attrfile / $attrname 
	if [ $? == 0 ]; then
		# must be fail
		exit $exit_fail
	fi
}

# xml xattr test - 2
{
	gfxattr -gx / $attrname 
	if [ $? == 0 ]; then
		# must be fail
		exit $exit_fail
	fi
}

# xml xattr test - 3
{
	gfxattr -lx / 
	if [ $? == 0 ]; then
		# must be fail
		exit $exit_fail
	fi
}

# xml xattr test - 4
{
	gfxattr -rx / $attrname 
	if [ $? == 0 ]; then
		# must be fail
		exit $exit_fail
	fi
}

# xml xattr test - 5
{
	gffindxmlattr -d 0 /a / 
	if [ $? == 0 ]; then
		# must be fail
		exit $exit_fail
	fi
}
