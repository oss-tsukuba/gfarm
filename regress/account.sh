# this file itself is not a shell script, but contains common subroutines.

. ./regress.conf

fmt_init()
{
bgfmt="--- %-60.60s %s\n"
lgfmt="@:= %-60.60s %s\n"
fmt="%-60.60s ... "
fin="------------------------------------------------------------ --- ----"
}

print_header()
{
		printf -- "$bgfmt" "$tst" "BEGIN" 
		printf --   "$fmt" "$tst" >&2
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
		printf -- "$lgfmt" "$tst" "PASS"
		echo			  "PASS" >&2;;
	$exit_fail)
		printf -- "$lgfmt" "$tst" "FAIL"
		echo			  "FAIL" >&2;;
	$exit_xpass)
		printf -- "$lgfmt" "$tst" "XPASS"
		echo			  "XPASS" >&2;;
	$exit_xfail)
		printf -- "$lgfmt" "$tst" "XFAIL"
		echo			  "XFAIL" >&2;;
	$exit_unresolved)
		printf -- "$lgfmt" "$tst" "UNRESOLVED"
		echo			  "UNRESOLVED" >&2;;
	$exit_untested)
		printf -- "$lgfmt" "$tst" "UNTESTED"
		echo			  "UNTESTED" >&2;;
	$exit_unsupported)
		printf -- "$lgfmt" "$tst" "UNSUPPORTED"
		echo			  "UNSUPPORTED" >&2;;
	$exit_trap)
		printf -- "$lgfmt" "$tst" "KILLED"
		echo			  "KILLED" >&2;;
	*)	printf -- "$lgfmt" "$tst" "exit code = $exit_code"
		echo			  "exit code = $exit_code" >&2
		exit_code=$exit_trap;;
	esac

	return $exit_code
}

treat_as_untested()
{
		printf -- "$lgfmt" "$tst" "UNTESTED"
		echo			  "UNTESTED" >&2
}

print_summary()
{

	awk '
	/^@:= / { n++; count[$NF]++; }
	END {
		unresolved = n - count["PASS"] - count["FAIL"] - count["XPASS"] - count["XFAIL"] - count["UNTESTED"] - count["UNSUPPORTED"]

		printf "\n"
		printf "Total test: %d\n", n
		printf "  success            : %d\n", count["PASS"]
		printf "  failure            : %d\n", count["FAIL"]
	    if (count["XPASS"] > 0)
		printf "  unexpected success : %d\n", count["XPASS"]
	    if (count["XFAIL"] > 0)
		printf "  expected failure   : %d\n", count["XFAIL"]
	    if (unresolved > 0)
		printf "  unresolved         : %d\n", unresolved
	    if (count["UNTESTED"] > 0)
		printf "  untested           : %d\n", count["UNTESTED"]
	    if (count["UNSUPPORTED"] > 0)
		printf "  unsupported        : %d\n", count["UNSUPPORTED"]

		if (count["FAIL"] == 0 && unresolved == 0)
			exit('$exit_pass')
		else
			exit('$exit_fail')
	}' $*
	exit_code=$?
}

exitcode_by_summary()
{
	exit $exit_code
}
