# normal xattr limit test - 1
{
	echo -n "" > $attrfile
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

# normal xattr limit test - 2
{
	attrfileBig="/etc/services"
	gfxattr -s -f $attrfileBig / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -g -f $getfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfileBig $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# normal xattr limit test - 3
{
	attrfileBig="/usr/local/sbin/gfmd"
	gfxattr -s -f $attrfileBig / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -g -f $getfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfileBig $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

longname256="1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456"
longname257="12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567"

# normal xattr limit test - 4
{
	gfxattr -s -f $attrfile / $longname256
	if [ $? != 0 ]; then
		# must succeed
		exit $exit_fail
	fi
	gfxattr -g / $longname256
	if [ $? != 0 ]; then
		# must succeed
		exit $exit_fail
	fi
	gfxattr -r / $longname256
	if [ $? != 0 ]; then
		# must succeed
		exit $exit_fail
	fi
}

# normal xattr limit test - 5
{
	gfxattr -s -f $attrfile / $longname257
	if [ $? == 0 ]; then
		# must fail
		exit $exit_fail
	fi
	gfxattr -g / $longname257
	if [ $? == 0 ]; then
		# must fail
		exit $exit_fail
	fi
	gfxattr -r / $longname257
	if [ $? == 0 ]; then
		# must fail
		exit $exit_fail
	fi
}

