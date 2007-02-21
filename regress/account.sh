# this file itself is not a shell script, but contains common subroutines.

. ./regress.conf

# constants
account_bgfmt="--- %-60.60s %s\n"
account_lgfmt="@:= %-60.60s %s\n"
account_fmt="%-60.60s ... "
account_fin="--- ------------------------------------------------------------ ----"

print_both()
{
	printf "%s\n" "$*" >&2
	printf "%s\n" "$*"
}

print_header_stderr()
{
		printf -- "$account_fmt"   "$tst" >&2
}

print_header()
{
		printf -- "$account_bgfmt" "$tst" "BEGIN"
		echo "@@_ start at `date +'%s'` - `date`"
		print_header_stderr
}

print_footer()
{
		echo "@@~  end  at `date +'%s'` - `date`"
		echo "$account_fin"
}

eval_result()
{
	exit_code=$1

	case $exit_code in
	$exit_pass)
		printf -- "$account_lgfmt" "$tst" "PASS"
		echo >&2			  "PASS";;
	$exit_fail)
		printf -- "$account_lgfmt" "$tst" "FAIL"
		echo >&2			  "FAIL";;
	$exit_xpass)
		printf -- "$account_lgfmt" "$tst" "XPASS"
		echo >&2			  "XPASS";;
	$exit_xfail)
		printf -- "$account_lgfmt" "$tst" "XFAIL"
		echo >&2			  "XFAIL";;
	$exit_unresolved)
		printf -- "$account_lgfmt" "$tst" "UNRESOLVED"
		echo >&2			  "UNRESOLVED";;
	$exit_untested)
		printf -- "$account_lgfmt" "$tst" "UNTESTED"
		echo >&2			  "UNTESTED";;
	$exit_unsupported)
		printf -- "$account_lgfmt" "$tst" "UNSUPPORTED"
		echo >&2			  "UNSUPPORTED";;
	$exit_trap)
		printf -- "$account_lgfmt" "$tst" "KILLED"
		echo >&2			  "KILLED";;
	*)	printf -- "$account_lgfmt" "$tst" "exit code = $exit_code"
		echo >&2			  "exit code = $exit_code"
		exit_code=$exit_trap;;
	esac

	return $exit_code
}

# do not print header and footer, but only result.
# currently this function is only called from fuse_test.sh.
print_result()
{
	print_header_stderr
	eval_result "$@"
}

treat_as_untested()
{
		printf -- "$account_lgfmt" "$tst" "UNTESTED"
		echo >&2			  "UNTESTED"
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
