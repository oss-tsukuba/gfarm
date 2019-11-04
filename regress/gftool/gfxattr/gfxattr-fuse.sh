# fuse test - 1
{
	echo HelloFuse1 > $attrfile
	cat $attrfile | attr -q -s $attrname $fusemnt
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	attr -q -g $attrname $fusemnt > $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# fuse test - 2
{
	echo $attrname > $nameslist
	attr -q -l $fusemnt > $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# fuse test - 3
{
	attr -q -r $attrname $fusemnt
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# fuse test - 4
{
	echo HelloFuse4 > $attrfile
	touch $fusemnt/$fileX
	cat $attrfile | attr -q -s $attrname $fusemnt/$fileX
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	attr -q -g $attrname $fusemnt/$fileX > $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $attrfile $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# fuse test - 5
{
	echo $attrname > $nameslist
	attr -q -l $fusemnt/$fileX > $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getnames
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# fuse test - 6
{
	attr -q -r $attrname $fusemnt/$fileX
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}
