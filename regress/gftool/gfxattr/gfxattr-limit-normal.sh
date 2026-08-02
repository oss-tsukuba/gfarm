# normal xattr limit test - 1
{
	echo -n "" > $attrfile
	gfxattr -s -f $attrfile / $attrname
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	wait_for_command_output $getfile $attrfile -- gfxattr -g -f $getfile / $attrname || {
		exit $exit_fail
	}
}

# normal xattr limit test - 2
{
	attrfileBig="$xattrtmp/limit-normal-big"
	# 64KiB ... GFARM_XATTR_SIZE_MAX_DEFAULT
	awk 'BEGIN {
		for (i = 0; i < 1024; i++) {
			printf "%s", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-"
		}
	}' > $attrfileBig
	gfxattr -s -f $attrfileBig / $attrname
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	wait_for_command_output $getfile $attrfileBig -- gfxattr -g -f $getfile / $attrname || {
		exit $exit_fail
	}
}

# normal xattr limit test - 3
#{
#	attrfileBig="/usr/local/sbin/gfmd"
#	[ -f $attrfileBig ] || return
#	gfxattr -s -f $attrfileBig / $attrname
#	if [ $? != 0 ]; then
#		exit $exit_fail
#	fi
#	gfxattr -g -f $getfile / $attrname
#	if [ $? != 0 ]; then
#		exit $exit_fail
#	fi
#	cmp $attrfileBig $getfile
#	if [ $? != 0 ]; then
#		exit $exit_fail
#	fi
#}

longname256="user.12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901"
longname257="user.123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012"

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
	if [ $? = 0 ]; then
		# must fail
		exit $exit_fail
	fi
	gfxattr -g / $longname257
	if [ $? = 0 ]; then
		# must fail
		exit $exit_fail
	fi
	gfxattr -r / $longname257
	if [ $? = 0 ]; then
		# must fail
		exit $exit_fail
	fi
}
