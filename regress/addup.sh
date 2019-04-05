#!/bin/sh

schedule=
show_each=1
show_total=1
max_test_name_width=0
verbose=0

usage()
{
	echo >&2 "Usage: $0 [-ETv] [-s <schedule>] [-[wW] <width>] " \
		"<log1> <log2>..."
	exit 2
}

while getopts ETWs:w:v OPT; do
	case $OPT in
	E)	show_total=0;;
	T)	show_each=0;;
	W)	max_test_name_width=$OPTARG;;
	s)	schedule=$OPTARG;;
	v)	verbose=1;;
	w)	COLUMNS=$OPTARG;;
	?)	usage;;
	esac
done
shift `expr $OPTIND - 1`

case $# in
0)	usage;;
esac

# determine number of columns of the terminal
case ${COLUMNS+set} in
set)	width=$COLUMNS;;
*)	if [ -t 2 ] && width=`tput cols`; then :; else width=80; fi;;
esac

awk '
BEGIN {
	show_each='${show_each}'
	show_total='${show_total}'
	schedule="'"${schedule}"'"
	width='${width}'
	max_test_name_width='${max_test_name_width}'
	verbose='${verbose}'
	old_filename = ""
	n_logfile = 0
	test_index = 0
	has_schedule = 0
}
FILENAME == schedule {
	test = $0
	sub(/#.*/, "", test)
	gsub(/ /, "", test)
	if (test == "")
		next

	test_name[test_index] = test
	test_index++

	old_filename = schedule
	has_schedule = 1
	next
}
FILENAME != old_filename {
	old_filename = FILENAME
	n_logfile++

}
FILENAME != schedule && /^@:=/ {
	test = $2
	result = $3

	if (!has_schedule && !(test in appeared)) {
		test_name[test_index] = test
		test_index++
		appeared[test] = 1
	}

	all[test, n_logfile] = result
}
END {
	n_pass = n_fail = n_xpass = n_xfail = 0
	n_unresolved = n_untested = n_unsupported = 0

	test_name_width = result_width = 0
	for (i = 0; i < test_index; i++) {
		test = test_name[i]

		others = "UNTESTED"
		for (j = 1; j <= n_logfile; j++) {
			result = all[test, j]
			if (result == "")
				result = all[test, j] = "UNTESTED"
			if (others == "UNSUPPORTED" && result == "UNTESTED") {
				# UNSUPPORTED takes precedence over UNTESTED:
				# keep others UNSUPPORTED
			} else if (others == "UNSUPPORTED" ||
			    others == "UNTESTED") {
				others = result
			} else if (others == result) {
				# do nothing
			} else if (result != "UNSUPPORTED" &&
			    result != "UNTESTED") {
				others = "SHOW-ALL"
			}

			if (result == "PASS")
				n_pass++;
			if (result == "FAIL")
				n_fail++;
			if (result == "XPASS")
				n_xpass++;
			if (result == "XFAIL")
				n_xfail++;
			if (result == "UNRESOLVED")
				n_unresolved++;
			if (result == "UNTESTED")
				n_untested++;
			if (result == "UNSUPPORTED")
				n_unsupported++;
		}
		if (verbose) {
			others = sprintf("%-11s", all[test, 1])
			for (j = 2; j <= n_logfile; j++)
				others = sprintf("%s/%-11s",
				    others, all[test, j])
		} else if (others == "SHOW-ALL") {
			others = all[test, 1]
			for (j = 2; j <= n_logfile; j++)
				others = others "/" all[test, j]
		}

		display[test] = others
		if (test_name_width < length(test))
			test_name_width = length(test)
		if (result_width < length(others))
			result_width = length(others)
	}
	if (show_each) {
		width -= result_width + 6 # 6 is for " ... " and " " at EOL
		if (width < 0)
			width = 1
		if (width > test_name_width)
			width = test_name_width
		if (max_test_name_width != 0 && width > max_test_name_width)
			width = max_test_name_width
		for (i = 0; i < test_index; i++) {
			test = test_name[i]
			printf "%-" width "." width "s ... %s\n",
				test, display[test]
		}
	}
	if (show_each && show_total)
		printf "\n"
	if (show_total) {
		print	      "Total test           : ",
				n_pass + n_fail + n_xpass + n_xfail
		print	      "  success            : ", n_pass
		print	      "  failure            : ", n_fail
		if (n_xpass > 0)
			print "  unexpected success : ", n_xpass
		if (n_xfail > 0)
			print "  expected failure   : ", n_xfail
		if (n_unresolved > 0)
			print "  unresolved         : ", n_unresolved
		if (n_untested > 0)
			print "  untested           : ", n_untested
		if (n_unsupported > 0)
			print "  unsupported        : ", n_unsupported
	}
}' $schedule $*
