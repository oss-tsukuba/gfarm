# xml xattr stat test - 1
{
	gfstat / > $statfile
	echo 'Hello1' > $attrfile
	gfxattr -sx -f $attrfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	wait_for_gfstat_change $getstat $statfile || {
		# ctime must be changed 
		exit $exit_fail
	}
	ret=`diff $statfile $getstat | grep '<' | grep -v Change | wc -l`
	if [ $ret != 0 ]; then
		# ctime must be changed 
		exit $exit_fail
	fi
}

# xml xattr stat test - 2
{
	gfstat / > $statfile
	echo 'Hello2' > $attrfile
	wait_for_command_success -- gfxattr -gx -f $getfile / $attrname || {
		exit $exit_fail
	}
	wait_for_gfstat_same $getstat $statfile || {
		# nothing must be changed 
		exit $exit_fail
	}
}

# xml xattr stat test - 3
{
	gfstat / > $statfile
	gfxattr -lx / 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	wait_for_gfstat_same $getstat $statfile || {
		# nothing must be changed 
		exit $exit_fail
	}
}

# xml xattr stat test - 4
{
	gfstat / > $statfile
	gfxattr -rx / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	wait_for_gfstat_change $getstat $statfile || {
		# ctime must be changed 
		exit $exit_fail
	}
	ret=`diff $statfile $getstat | grep '<' | grep -v Change | wc -l`
	if [ $ret != 0 ]; then
		# ctime must be changed 
		exit $exit_fail
	fi
}
