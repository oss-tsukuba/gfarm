#!/bin/sh

puts_error() {
    echo "ERR: $1" 1>&2
}

run_test() {
    _r=1
    test_id=$1
    result=""
    expected_result=""
    expected_result_csv="`dirname $0`/expected-test-result.csv"

    $2 > /dev/null 2>&1 &

    while :
    do
        netstat -an | grep LISTEN | grep :12345 > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            break
        fi
    done

    $3 > /dev/null 2>&1
    result=$?

    expected_result=`cat ${expected_result_csv} | grep -E "^${test_id}" | awk -F "," '{print $2}' | sed 's:\r$::'`
    if [ "${result}" = "${expected_result}" ]; then
        echo "${test_id}: OK"
        _r=0
    else
        echo "${test_id}: NG"
    fi

    return ${_r}
}
