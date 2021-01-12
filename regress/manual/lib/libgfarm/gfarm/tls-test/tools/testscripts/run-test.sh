#!/bin/sh

_ret=1
fail_flag=0
expected_result_csv="expected-test-result.csv"

if [ ! -f ${expected_result_csv} ]; then
    echo "not exist csv file." 1>&2
    exit 1
fi

# test4
./test4.sh
if [ $? -ne 0 ]; then
    fail_flag=1
fi

if [ ${fail_flag} -eq 0 ]; then
    _ret=0
fi

exit ${_ret}
