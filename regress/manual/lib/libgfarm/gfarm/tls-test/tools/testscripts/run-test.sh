#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
total_tests_num=`cat ${TOP_DIR}/expected-test-result.csv | grep -v -E "^3|^[a-zA-Z]" | wc -l`
total_fail_num=0

source ${TOP_DIR}/lib/funcs.sh


usage(){
	cat << EOS >&2
Usage:

	OPTION:
		-t TEST_NUMBER		Execute only TEST_NUMBER test
		-d			Debug flag
		-h			Help
EOS
exit 0
}

_ret=1
fail_flag=0
expected_result_csv="expected-test-result.csv"
exec_test_num=0
debug_flag=0
ENV_DIR="${TOP_DIR}/../../test_dir"

## Opts. ##
while getopts t:hd OPT; do
	case ${OPT} in
		t) exec_test_num=${OPTARG};;
		d) debug_flag=1;;
		h) usage;;
		*) usage;;
	esac
done
shift `expr $OPTIND - 1`

if [ ! -f ${TOP_DIR}/${expected_result_csv} ]; then
	puts_error "not exist csv file."
	exit 1
fi

if [ ! -d ${ENV_DIR} ]; then
	puts_error "not exist test_dir."
	exit 1
fi

if [ ${exec_test_num} -ne 0 \
	-a ${exec_test_num} -ne 1 \
	-a ${exec_test_num} -ne 2 \
	-a ${exec_test_num} -ne 4 \
	-a ${exec_test_num} -ne 5 \
	-a ${exec_test_num} -ne 7 \
	-a ${exec_test_num} -ne 8 \
	-a ${exec_test_num} -ne 9 \
	-a ${exec_test_num} -ne 10 \
	-a ${exec_test_num} -ne 11 \
	-a ${exec_test_num} -ne 12 ]; then
		puts_error "wrong argument."
		exit 1
fi

# test 1
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 1 ]; then
	if [ ${debug_flag} -eq 1 ]; then
		${TOP_DIR}/test1.sh -d
		test1_fail_num=$?
	else
		${TOP_DIR}/test1.sh
		test1_fail_num=$?
	fi
	if [ ${test1_fail_num} -ne 0 ]; then
		fail_flag=1
		total_fail_num=`expr ${total_fail_num} + ${test1_fail_num}`
	fi
fi

# test 2
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 2 ]; then
	if [ ${debug_flag} -eq 1 ]; then
		${TOP_DIR}/test2.sh -d
		test2_fail_num=$?
	else
		${TOP_DIR}/test2.sh
		test2_fail_num=$?
	fi
	if [ ${test2_fail_num} -ne 0 ]; then
		fail_flag=1
		total_fail_num=`expr ${total_fail_num} + ${test2_fail_num}`
	fi
fi

# test 4
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 4 ]; then
	if [ ${debug_flag} -eq 1 ]; then
		${TOP_DIR}/test4.sh -d
		test4_fail_num=$?
	else
		${TOP_DIR}/test4.sh
		test4_fail_num=$?
	fi
	if [ ${test4_fail_num} -ne 0 ]; then
		fail_flag=1
		total_fail_num=`expr ${total_fail_num} + ${test4_fail_num}`
	fi
fi

# test 5
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 5 ]; then
	if [ ${debug_flag} -eq 1 ]; then
		${TOP_DIR}/test5.sh -d
		test5_fail_num=$?
	else
		${TOP_DIR}/test5.sh
		test5_fail_num=$?
	fi
	if [ ${test5_fail_num} -ne 0 ]; then
		fail_flag=1
		total_fail_num=`expr ${total_fail_num} + ${test5_fail_num}`
	fi
fi

# test 7
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 7 ]; then
	if [ ${debug_flag} -eq 1 ]; then
		${TOP_DIR}/test7.sh -d
		test7_fail_num=$?
	else
		${TOP_DIR}/test7.sh
		test7_fail_num=$?
	fi
	if [ ${test7_fail_num} -ne 0 ]; then
		fail_flag=1
		total_fail_num=`expr ${total_fail_num} + ${test7_fail_num}`
	fi
fi

# test 8
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 8 ]; then
	if [ ${debug_flag} -eq 1 ]; then
		${TOP_DIR}/test8.sh -d
		test8_fail_num=$?
	else
		${TOP_DIR}/test8.sh
		test8_fail_num=$?
	fi
	if [ ${test8_fail_num} -ne 0 ]; then
		fail_flag=1
		total_fail_num=`expr ${total_fail_num} + ${test8_fail_num}`
	fi
fi

# test 9
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 9 ]; then
	if [ ${debug_flag} -eq 1 ]; then
		${TOP_DIR}/test9.sh -d
		test9_fail_num=$?
	else
		${TOP_DIR}/test9.sh
		test9_fail_num=$?
	fi
	if [ ${test9_fail_num} -ne 0 ]; then
		fail_flag=1
		total_fail_num=`expr ${total_fail_num} + ${test9_fail_num}`
	fi
fi

# test 10
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 10 ]; then
	if [ ${debug_flag} -eq 1 ]; then
		${TOP_DIR}/test10.sh -d
		test10_fail_num=$?
	else
		${TOP_DIR}/test10.sh
		test10_fail_num=$?
	fi
	if [ ${test10_fail_num} -ne 0 ]; then
		fail_flag=1
		total_fail_num=`expr ${total_fail_num} + ${test10_fail_num}`
	fi
fi

# test 11
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 11 ]; then
	if [ ${debug_flag} -eq 1 ]; then
		${TOP_DIR}/test11.sh -d
		test11_fail_num=$?
	else
		${TOP_DIR}/test11.sh
		test11_fail_num=$?
	fi
	if [ ${test11_fail_num} -ne 0 ]; then
		fail_flag=1
		total_fail_num=`expr ${total_fail_num} + ${test11_fail_num}`
	fi
fi

# test 12
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 12 ]; then
	if [ ${debug_flag} -eq 1 ]; then
		${TOP_DIR}/test12.sh -d
		test12_fail_num=$?
	else
		${TOP_DIR}/test12.sh
		test12_fail_num=$?
	fi
	if [ ${test12_fail_num} -ne 0 ]; then
		fail_flag=1
		total_fail_num=`expr ${total_fail_num} + ${test12_fail_num}`
	fi
fi

if [ ${fail_flag} -eq 0 ]; then
	_ret=0
	if [ ${exec_test_num} -eq 0 ]; then
		total_pass_num=`expr ${total_tests_num} - ${total_fail_num}`
		echo ""
		echo "PASS:	${total_pass_num}/${total_tests_num}"
		echo "FAIL:	${total_fail_num}/${total_tests_num}"
		echo "All the tests succeeded."
	fi
elif [ ${exec_test_num} -eq 0 ]; then
	total_pass_num=`expr ${total_tests_num} - ${total_fail_num}`
	echo ""
	echo "PASS:	${total_pass_num}/${total_tests_num}"
	echo "FAIL:	${total_fail_num}/${total_tests_num}"	
fi

exit ${_ret}
