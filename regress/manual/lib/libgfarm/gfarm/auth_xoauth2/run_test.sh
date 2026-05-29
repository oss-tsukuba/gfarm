#!/bin/bash
set -euo pipefail

INIT_CONF="init_gfarm_sasl.conf"
CONF="gfarm_sasl.conf"
DEPLOY="./deploy_gfarm_sasl.sh"

LOG_ROOT="auth_xoauth2_test"
mkdir -p "$LOG_ROOT"

if ! grep -qx 'log_auth_verbose enable' ~/.gfarm2rc 2>/dev/null; then
    echo '[INFO] add "log_auth_verbose enable" to ~/.gfarm2rc'
    echo 'log_auth_verbose enable' >> ~/.gfarm2rc
fi
echo "[INFO] executing authconfig sasl.xoauth2"
authconfig sasl.xoauth2

PASS_COUNT=0
FAIL_COUNT=0
TOTAL_COUNT=0
FAIL_LIST=()

run_test() {

    ID="$1"
    NAME="$2"
    CHANGES="$3"
    EXPECT="$4"

    TEST_DIR="${LOG_ROOT}/test_${ID}"
    mkdir -p "$TEST_DIR"

    echo "==================================="
    echo "TEST $ID : $NAME"
    echo "==================================="

    cp "$INIT_CONF" "$CONF"

    IFS=';' read -ra LINES <<< "$CHANGES"

    for LINE in "${LINES[@]}"
    do
        KEY=${LINE%%:*}
        VALUE=${LINE#*:}

        KEY=$(echo "$KEY" | xargs)
        VALUE=$(echo "$VALUE" | xargs)

        if grep -q "^${KEY}:" "$CONF"; then
            sed -i "s~^${KEY}:.*~${KEY}: ${VALUE}~" "$CONF"
        else
            echo "${KEY}: ${VALUE}" >> "$CONF"
        fi
    done

    cp "$CONF" "$TEST_DIR/gfarm_sasl.conf"

    $DEPLOY < /dev/null > "${TEST_DIR}/deploy.log" 2>&1

    gfservice restart-all

    echo
    echo "-- input --"
    cat "$CONF"

    echo
    echo "-- output --"

    RC=0

    set +e
    gfhost -l 2>&1 | tee "${TEST_DIR}/gfhost.log" || RC=${PIPESTATUS[0]}
    set -e

    XOAUTH2_COUNT=$(
    grep -c "SASL using mechanism XOAUTH2" \
        "${TEST_DIR}/gfhost.log" || true
    )
    NODE_COUNT=$(grep -c ".*/.*/.* A " "${TEST_DIR}/gfhost.log" || true)

    if [[ $XOAUTH2_COUNT -ge 1 &&
      $XOAUTH2_COUNT -eq $((NODE_COUNT + 1)) ]]
    then
        USING_XOAUTH2=true
    else
        USING_XOAUTH2=false
    fi

    if [[ "$EXPECT" == "success" ]]; then
        if [[ $RC -eq 0 && $USING_XOAUTH2 == true ]]; then
            RESULT="PASS"
        else
            RESULT="FAIL"
        fi

    else

        if [[ $RC -ne 0 ]]; then
            RESULT="PASS"
        else
            RESULT="FAIL"
        fi

    fi

    {
        echo "ID: $ID"
        echo "NAME: $NAME"
        echo "CHANGES: $CHANGES"
        echo "EXPECT: $EXPECT"
        echo "EXIT_CODE: $RC"
        echo "USING_XOAUTH2: $USING_XOAUTH2"
        echo "RESULT: $RESULT"
    } > "${TEST_DIR}/result.log"

    TOTAL_COUNT=$((TOTAL_COUNT+1))

    if [[ "$RESULT" == "PASS" ]]; then
        PASS_COUNT=$((PASS_COUNT+1))
    else
        FAIL_COUNT=$((FAIL_COUNT+1))
        FAIL_LIST+=("$ID:$NAME")
    fi

    echo "RESULT: $RESULT"
    echo
}

TEST_TABLE="tests.table"

while IFS='^' read -r ID NAME CHANGES EXPECT || [[ -n "${ID:-}" ]]
do
    [[ -z "$ID" ]] && continue
    [[ "$ID" =~ ^# ]] && continue

    run_test "$ID" "$NAME" "$CHANGES" "$EXPECT"
done < "$TEST_TABLE"

echo "================================="
echo "TEST SUMMARY"
echo "================================="

echo "TOTAL : $TOTAL_COUNT"
echo "PASS  : $PASS_COUNT"
echo "FAIL  : $FAIL_COUNT"

if [[ $FAIL_COUNT -ne 0 ]]; then
    echo
    echo "FAILED TESTS:"
    for t in "${FAIL_LIST[@]}"
    do
        echo "  $t"
    done
fi

cleanup() {
    if grep -qx 'log_auth_verbose enable' ~/.gfarm2rc 2>/dev/null; then
        echo '[INFO] removing "log_auth_verbose enable" from ~/.gfarm2rc'
        sed -i '\~^log_auth_verbose enable$~d' ~/.gfarm2rc
    fi
}

echo
echo "logs are saved under: $LOG_ROOT/"
trap cleanup EXIT
