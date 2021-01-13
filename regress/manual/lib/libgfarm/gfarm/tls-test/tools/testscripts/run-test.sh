#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`

source ${TOP_DIR}/lib/funcs.sh

usage(){
    cat << EOS >&2
Usage:

    OPTION:
        -t TEST_NUMBER	Execute only TEST_NUMBER test
        -h		Help
EOS
exit 0
}

_ret=1
fail_flag=0
expected_result_csv="expected-test-result.csv"
exec_test_num=0
ENV_DIR="${TOP_DIR}/../../gfarm_environment"

## Opts. ##
while getopts t:h OPT; do
    case ${OPT} in
        t) exec_test_num=${OPTARG};;
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
    puts_error "not exist gfarm_environment."
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
    -a ${exec_test_num} -ne 10 ]; then
        puts_error "wrong argument."
        exit 1
fi

# test 1
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 1 ]; then
    ${TOP_DIR}/test1.sh
    if [ $? -ne 0 ]; then
        fail_flag=1
    fi
fi

# test 2
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 2 ]; then
    ${TOP_DIR}/test2.sh
    if [ $? -ne 0 ]; then
        fail_flag=1
    fi
fi

# test 4
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 4 ]; then
    ${TOP_DIR}/test4.sh
    if [ $? -ne 0 ]; then
        fail_flag=1
    fi
fi

# test 5
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 5 ]; then
    ${TOP_DIR}/test5.sh
    if [ $? -ne 0 ]; then
        fail_flag=1
    fi
fi

# test 7
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 7 ]; then
    ${TOP_DIR}/test7.sh
    if [ $? -ne 0 ]; then
        fail_flag=1
    fi
fi

# test 8
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 8 ]; then
    ${TOP_DIR}/test8.sh
    if [ $? -ne 0 ]; then
        fail_flag=1
    fi
fi

# test 9
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 9 ]; then
    ${TOP_DIR}/test9.sh
    if [ $? -ne 0 ]; then
        fail_flag=1
    fi
fi

# test 10
if [ ${exec_test_num} -eq 0 -o ${exec_test_num} -eq 10 ]; then
    ${TOP_DIR}/test10.sh
    if [ $? -ne 0 ]; then
        fail_flag=1
    fi
fi

if [ ${fail_flag} -eq 0 ]; then
    _ret=0
fi

exit ${_ret}
