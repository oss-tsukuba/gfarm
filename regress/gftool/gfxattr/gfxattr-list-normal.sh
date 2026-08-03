# normal xattr list test - 1
{
	echo $attrname > $nameslist
	wait_for_command_output $getnames $nameslist -- gfxattr -l /
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
	wait_for_command_output $getnames $nameslist -- gfxattr -l / || {
		exit $exit_fail
	}
}

# normal xattr list test - 3
{
	echo $attrname > $nameslist
	wait_for_command_output $getnames $nameslist -- gfxattr -l $fileX
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
	wait_for_command_output $getnames $nameslist -- gfxattr -l $fileX || {
		exit $exit_fail
	}
}
