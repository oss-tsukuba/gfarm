# this is not shell script, but common subrotines

. ./regress.conf

clear_counters()
{
	pass=0
	fail=0
	xpass=0
	xfail=0
	unresolved=0
	untested=0
	unsupported=0
}

fmt_init()
{
fmt="%-60.60s ... %s"
fin="------------------------------------------------------------ --- ----"
}

print_header()
{
		printf "$fmt" "$tst"; echo "BEGIN" 
		printf "$fmt" "$tst" >&2
}

print_footer()
{
		echo "$fin"
}

eval_result()
{
	exit_code=$1

	case $exit_code in
	$exit_pass)
		printf "$fmt" "$tst";	echo "PASS"
					echo "PASS" >&2
		pass=`expr $pass + 1`;;
	$exit_fail)
		printf "$fmt" "$tst";	echo "FAIL"
					echo "FAIL" >&2
		fail=`expr $fail + 1`;;
	$exit_xpass)
		printf "$fmt" "$tst";	echo "XPASS"
					echo "XPASS" >&2
		xpass=`expr $xpass + 1`;;
	$exit_xfail)
		printf "$fmt" "$tst";	echo "XFAIL"
					echo "XFAIL" >&2
		xfail=`expr $xfail + 1`;;
	$exit_unresolved)
		printf "$fmt" "$tst";	echo "UNRESOLVED"
					echo "UNRESOLVED" >&2
		unresolved=`expr $unresolved + 1`;;
	$exit_untested)
		printf "$fmt" "$tst";	echo "UNTESTED"
					echo "UNTESTED" >&2
		untested=`expr $untested + 1`;;
	$exit_unsupported)
		printf "$fmt" "$tst";	echo "UNSUPPORTED"
					echo "UNSUPPORTED" >&2
		unsupported=`expr $unsupported + 1`;;
	$exit_trap)
		printf "$fmt" "$tst";	echo "KILLED"
					echo "KILLED" >&2;;
	*)	printf "$fmt" "$tst";	echo "exit code = $exit_code"
					echo "exit code = $exit_code" >&2
		exit_code=$exit_trap;;
	esac

	return $exit_code
}

treat_as_untested()
{
		printf "$fmt" "$tst";	echo "UNTESTED"
					echo "UNTESTED" >&2
		untested=`expr $untested + 1`
}

print_summary()
{
	echo ""
	echo "Total test: `expr $pass + $fail + $xpass + $xfail + $unresolved + $untested + $unsupported`"
	echo "  success         : $pass"
	echo "  failure         : $fail"

    [ $xpass -ne 0 ] &&
	echo "  unexpected pass : $xpass"
    [ $xfail -ne 0 ] &&
	echo "    expected fail : $xfail"
    [ $unresolved -ne 0 ] &&
	echo "  unresolved      : $unresolved"
    [ $untested -ne 0 ] &&
	echo "  untested : $untested"
    [ $unsupported -ne 0 ] &&
	echo "  unsupported     : $unsupported"
}

exitcode_by_summary()
{
	if [ $fail -eq 0 ] && [ $unresolved -eq 0 ]; then
		exit $exit_pass
	else
		exit $exit_fail
	fi
}
