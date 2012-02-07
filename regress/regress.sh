#!/bin/sh

. ./regress.conf

before_tst ()
{
	tst=$1
	log=$2
	jenkins_classname=$3

	account_fmt="%-60.60s ... "
	account_bgfmt="--- %-60.60s %s\n"

	if [ X"$jenkins_classname" = X"" ]; then
		printf -- "$account_fmt"   "$tst"
	fi    

	printf -- "$account_bgfmt" "$tst" "BEGIN" >>$log
	date +'@@_ start at %s - %Y-%m-%d %H:%M:%S' >>$log
} 

after_tst ()
{
	tst=$1
	log=$2
	result=$3
	elapsed_time=$4
	jenkins_classname=$5

	testcase_fmt_begin="    <testcase classname=\"%s\" name=\"%s\" time=\"%d\">\n"
	failure_fmt="        <failure message=\"%s\"/>\n"
	testcase_fmt_end="    </testcase>\n"
	account_lgfmt="@:= %-60.60s %s\n"

	if [ X"$jenkins_classname" = X"" ]; then
		echo $result
	else
		printf -- "$testcase_fmt_begin" "$jenkins_classname" "$tst" $elapsed_time
	    	case $result in
		"FAIL"|"TRAP"|"XPASS")    
			printf -- "$failure_fmt"    "$result"
		esac    
		printf -- "$testcase_fmt_end"
	fi    

	printf -- "$account_lgfmt" "$tst" "$result" >>$log
	date +'@@~  end  at %s - %Y-%m-%d %H:%M:%S' >>$log
	echo $account_fin >>$log
}

# constants
account_fin="--- ------------------------------------------------------------ ----"

log=log
rm -f $log

while getopts j: OPT; do
	case $OPT in
	"j") jenkins_classname=$OPTARG;
	     rm -f $jenkins_file;;
	esac
done

shift `expr $OPTIND - 1`

case $# in
0)	schedule=$regress/schedule;;
*)	schedule=$*;;
esac

n_pass=0
n_fail=0
n_xpass=0
n_xfail=0
n_unresolved=0
n_untested=0
n_unsupported=0
n_trap=0


if [ X"$jenkins_classname" != X"" ]; then
	echo "<testsuite>"
fi

while read line; do
	set x $line
	shift
	case $# in 0) continue;; esac
	case $1 in '#'*) continue;; esac

	tst=$1

	before_tst $tst $log "$jenkins_classname"
	start_time=`date +%s`

	if [ -x $regress/$tst ]; then

		sh $regress/$tst >>$log 2>&1
		exit_code=$?
		elapsed_time=`expr \`date +%s\` - $start_time`

		case $exit_code in
		$exit_pass)
			after_tst $tst $log "PASS" $elapsed_time "$jenkins_classname"
			n_pass=`expr $n_pass + 1`;;
		$exit_fail)
			after_tst $tst $log "FAIL" $elapsed_time "$jenkins_classname"
			n_fail=`expr $n_fail + 1`;;
		$exit_xpass)
			after_tst $tst $log "XPASS" $elapsed_time "$jenkins_classname"
			n_xpass=`expr $n_xpass + 1`;;
		$exit_xfail)
			after_tst $tst $log "XFAIL" $elapsed_time "$jenkins_classname"
			n_xfail=`expr $n_xfail + 1`;;
		$exit_unresolved)
			after_tst $tst $log "UNRESOLVED" $elapsed_time "$jenkins_classname"
			n_unresolved=`expr $n_unresolved + 1`;;
		$exit_untested)
			after_tst $tst $log "UNTESTED" $elapsed_time "$jenkins_classname"
			n_untested=`expr $n_untested + 1`;;
		$exit_unsupported)
			after_tst $tst $log "UNSUPPORTED" $elapsed_time "$jenkins_classname"
			n_unsupported=`expr $n_unsupported + 1`;;
		$exit_trap)
			after_tst $tst $log "KILLED" $elapsed_time "$jenkins_classname"
			n_trap=`expr $n_trap + 1`
			break;;
		*)
			echo                              
			after_tst $tst $log "exit($exit_code)" $elapsed_time "$jenkins_classname"
			n_trap=`expr $n_trap + 1`
			break;;
		esac
	else
			after_tst $tst $log "SKIPPED" "0" "$jenkins_classname"
			n_untested=`expr $n_untested + 1`
	fi

	
done < $schedule

if [ X"$jenkins_classname" != X"" ]; then
	echo '</testsuite>'
else
	echo ""
	echo "Total test: `expr $n_pass + $n_fail`"
	echo "  success            : $n_pass"
	echo "  failure            : $n_fail"

	if [ $n_xpass -gt 0 ]; then
	    echo "  unexpected success : $n_xpass"
	fi
	if [ $n_xfail -gt 0 ]; then
	    echo "  expected failure   : $n_xfail"
	fi
	if [ $n_unresolved -gt 0 ]; then
	    echo "  unresolved         : $n_unresolved"
	fi
	if [ $n_untested -gt 0 ]; then
	    echo "  untested           : $n_untested"
	fi
	if [ $n_unsupported -gt 0 ]; then
	    echo "  unsupported        : $n_unsupported"
	fi
fi

case $n_trap in 0) :;; *) exit $exit_trap;; esac
[ $n_fail -eq 0 -a $n_unresolved -eq 0 ]
