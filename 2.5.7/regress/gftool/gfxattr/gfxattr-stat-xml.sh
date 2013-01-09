# xml xattr stat test - 1
{
	gfstat / > $statfile
	sleep 1
	echo 'Hello1' > $attrfile
	gfxattr -sx -f $attrfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfstat / > $getstat
	cmp -s $statfile $getstat
	if [ $? == 0 ]; then
		# ctime must be changed 
		exit $exit_fail
	fi
	ret=`diff $statfile $getstat | grep '<' | grep -v Change | wc -l`
	if [ $ret != 0 ]; then
		# ctime must be changed 
		exit $exit_fail
	fi
}

# xml xattr stat test - 2
{
	gfstat / > $statfile
	sleep 1
	echo 'Hello2' > $attrfile
	gfxattr -gx -f $getfile / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfstat / > $getstat
	cmp -s $statfile $getstat
	if [ $? != 0 ]; then
		# nothing must be changed 
		exit $exit_fail
	fi
}

# xml xattr stat test - 3
{
	gfstat / > $statfile
	sleep 1
	gfxattr -lx / 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfstat / > $getstat
	cmp -s $statfile $getstat
	if [ $? != 0 ]; then
		# nothing must be changed 
		exit $exit_fail
	fi
}

# xml xattr stat test - 4
{
	gfstat / > $statfile
	sleep 1
	gfxattr -rx / $attrname 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfstat / > $getstat
	cmp -s $statfile $getstat
	if [ $? == 0 ]; then
		# ctime must be changed 
		exit $exit_fail
	fi
	ret=`diff $statfile $getstat | grep '<' | grep -v Change | wc -l`
	if [ $ret != 0 ]; then
		# ctime must be changed 
		exit $exit_fail
	fi
}
