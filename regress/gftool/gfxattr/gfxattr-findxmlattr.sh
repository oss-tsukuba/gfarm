gfrm $subsubdir/$fileX
gfrmdir $subsubdir
gfrm $subdir/$fileX
gfrmdir $subdir
gfrmdir $subdir2
gfrm $fileX
gfxattr -rx / $attrname
gfxattr -rx / $attrname2

echo '<a>aaa</a>' > $attrfile
echo '<b>bbb</b>' > $attrfile2

# xml xattr find test - 1.1
{
	gffindxmlattr /a / > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo -n "" > $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 1.2
{
	gfxattr -sx -f $attrfile / $attrname
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gffindxmlattr /a / > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 1.3
{
	gfxattr -sx -f $attrfile / $attrname
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gffindxmlattr /b / > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo -n "" > $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 1.4
{
	gfxattr -sx -f $attrfile / $attrname2
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gffindxmlattr /a / > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	echo "/	$attrname2" >> $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 1.5
{
	gfxattr -sx -f $attrfile2 / $attrname2
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gffindxmlattr /a / > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 1.6
{
	touch $fusemnt/$fileX 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfxattr -sx -f $attrfile $fileX $attrname
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gffindxmlattr /a / > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	echo "/$fileX	$attrname" >> $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 2.1
{
	gfmkdir $subdir
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gffindxmlattr /a /$subdir > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo -n "" > $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 2.2
{
	gfxattr -sx -f $attrfile $subdir $attrname
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	#
	# abs path
	gffindxmlattr /a /$subdir > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/$subdir	$attrname" > $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	#
	# relative path
	gffindxmlattr /a $subdir > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "$subdir	$attrname" > $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 2.3
{
	gffindxmlattr /a / | sort > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	echo "/$subdir	$attrname" >> $nameslist
	echo "/$fileX	$attrname" >> $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 3.1
{
	gfmkdir $subsubdir
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gffindxmlattr /a /$subsubdir > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo -n "" > $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 3.2
{
	gfxattr -sx -f $attrfile /$subsubdir $attrname
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gffindxmlattr /a /$subsubdir > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/$subsubdir	$attrname" > $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 3.3
{
	gffindxmlattr /a / | sort > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	echo "/$subdir	$attrname" >> $nameslist
	echo "/$subsubdir	$attrname" >> $nameslist
	echo "/file1	$attrname" >> $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 3.4
{
	gffindxmlattr -d 0 /a / | sort > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 3.5
{
	gffindxmlattr -d 1 /a / | sort > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	echo "/$subdir	$attrname" >> $nameslist
	echo "/file1	$attrname" >> $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 3.6
{
	gffindxmlattr -d 2 /a / | sort > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	echo "/$subdir	$attrname" >> $nameslist
	echo "/$subsubdir	$attrname" >> $nameslist
	echo "/file1	$attrname" >> $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 4.1
{
	gfmkdir $subdir2 
	gfmkdir $subdir2/$subdir2 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gffindxmlattr /a / | sort > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 5.1
{
	gfchmod 0655 $subdir
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gffindxmlattr /a / | sort > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	echo "/$subdir	$attrname" >> $nameslist
	echo "/$fileX	$attrname" >> $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 5.2
{
	gfchmod 0355 $subdir
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gffindxmlattr /a / | sort > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	echo "/$subsubdir	$attrname" >> $nameslist
	echo "/$fileX	$attrname" >> $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 5.3
{
	gfchmod 0555 $subdir
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gffindxmlattr /a / | sort > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	echo "/$subdir	$attrname" >> $nameslist
	echo "/$subsubdir	$attrname" >> $nameslist
	echo "/file1	$attrname" >> $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	gfchmod 0755 $subdir
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 6.1
{
	gffindxmlattr -F , /a / | sort > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/,$attrname" > $nameslist
	echo "/$subdir,$attrname" >> $nameslist
	echo "/$subsubdir,$attrname" >> $nameslist
	echo "/file1,$attrname" >> $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 6.2
{
	echo '/a' > $attrfile
	gffindxmlattr -f $attrfile / | sort > $getfile 
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	echo "/	$attrname" > $nameslist
	echo "/$subdir	$attrname" >> $nameslist
	echo "/$subsubdir	$attrname" >> $nameslist
	echo "/file1	$attrname" >> $nameslist
	cmp $nameslist $getfile
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# xml xattr find test - 6.3
{
	gffindxmlattr -d -1 /a / 
	if [ $? == 0 ]; then
		# must fail
		exit $exit_fail
	fi
}
