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
    result_server=0

    #$2 > /dev/null 2>&1 &

    sh -c "rm -f ./testfile.txt; $2 > /dev/null 2>&1; echo \$? > ./testfile.txt" &
    child_pid=$!
    while :
    do
        kill -0 ${child_pid}
        if [ $? -eq 0 ]; then
            break
        fi
    done

    while :
    do
        sleep 1
        kill -0 ${child_pid}
        if [ $? -ne 0 ]; then
            result_server=`cat ./testfile.txt`
            break
        fi
        netstat -an | grep LISTEN | grep :12345 > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            break
        fi
    done

    if [ ${result_server} -ne 0 ]; then
        case ${result_server} in
            2) echo "tls_context error";;
            3) echo "bind error";;
        esac
        return ${_r}
    fi

    sh -c "$3 > /dev/null 2>&1"
    #$3 > /dev/null 2>&1
    client_exitstatus=$?
    if [ ${client_exitstatus} -eq 2 -o ${client_exitstatus} -eq 3 ]; then
        kill -9 $child_pid
        while :
        do
            netstat -an | grep LISTEN | grep :12345 > /dev/null 2>&1
            if [ $? -ne 0 ]; then
                break
            fi
        done
        server_exitstatus=0
    else
    server_exitstatus=`cat ./testfile.txt`
    fi
    echo "server:$server_exitstatus"
    echo "client:$client_exitstatus"

    expected_server_result=`cat ${expected_result_csv} | grep -E "^${test_id}" | awk -F "," '{print $2}' | sed 's:\r$::'`
    expected_client_result=`cat ${expected_result_csv} | grep -E "^${test_id}" | awk -F "," '{print $3}' | sed 's:\r$::'`
    if [ "${server_exitstatus}" = "${expected_server_result}" -a "${client_exitstatus}" = "${expected_client_result}" ]; then
        echo "${test_id}: OK"
        _r=0
    else
        echo "${test_id}: NG"
    fi


    return ${_r}
}
