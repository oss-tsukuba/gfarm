# xml xattr test - 1
{
	echo "<a>aaa" > $attrfile
	gfxattr -sx -f $attrfile / $attrname
	if [ $? == 0 ]; then
		# must be fail because invalid XML data
		#exit $exit_fail
		echo invalid XML data should not be set
	fi
}

# xml xattr test - 2
{
	echo -n "" > $attrfile
	gfxattr -sx -f $attrfile / $attrname
	if [ $? == 0 ]; then
		# must be fail because rejected by PostgreSQL
		#exit $exit_fail
		echo empty XML data should not be set
	fi
}
