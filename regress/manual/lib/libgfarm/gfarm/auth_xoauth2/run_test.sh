#!/bin/bash
set -euo pipefail

CONF="gfarm_sasl.conf"
DEPLOY="./deploy_gfarm_sasl.sh"

LOG_ROOT="auth_xoauth2_test"
mkdir -p "$LOG_ROOT"

if ! grep -qx 'log_auth_verbose enable' ~/.gfarm2rc 2>/dev/null; then
    echo '[INFO] add "log_auth_verbose enable" to ~/.gfarm2rc'
    echo 'log_auth_verbose enable' >> ~/.gfarm2rc
fi

expand_wildcard()
{
    local issuer="$1"
    local line="$2"
    local token="$3"

    while [[ "$line" =~ @${token}\(([0-9]+)\)@ ]]
    do
        local n="${BASH_REMATCH[1]}"
        local replacement

        if (( n >= ${#issuer} )); then
            case "$token" in
            VALID_*)
                replacement="*"
                ;;
            *)
                replacement="invalid-none*"
                ;;
            esac
        else
            replacement="${issuer::${#issuer}-n}*"
        fi

        line="${line/@${token}($n)@/$replacement}"
    done

    printf '%s\n' "$line"
}

TMP_TABLE=$(mktemp)
SASL_CONF=$(pkg-config --variable=libdir libsasl2)/sasl2/gfarm-client.conf
INIT_CONF_TEMPLATE="init_gfarm_sasl.conf.in"
INIT_CONF="init_gfarm_sasl.conf"
if [[ $(gfhost -l 2>&1 | grep -c "SASL using mechanism XOAUTH2") -ge 1 ]] ; then
    echo "[INFO] XOAUTH2 mechanism is already enabled"
    VALID_AUD=$(jwt-parse | grep '"aud"' | sed -E 's/.*"aud": "([^"]+)".*/\1/')
    VALID_ISSUER=$(jwt-parse | grep '"iss"' | sed -E 's/.*"iss": "([^"]+)".*/\1/')
    INVALID_ISSUER="${VALID_ISSUER}-$(date +%s)"
    INVALID_ISSUER2="http://invalid-issuer.example.com/"
    ISSUER_PARENT=$(printf '%s\n' "$VALID_ISSUER" | sed 's:/[^/]*$::')
    VALID_CLAIM=$(
        sed -n 's/^xoauth2_user_claim:[[:space:]]*//p' "$SASL_CONF"
    )
    INVALID_CLAIM="$(date +%s)-invalid-claim"
    INVALID_CLAIM2="$(date +%s)-2-invalid-claim"
    VALID_SASL_USER=$(
    jwt-parse |
    sed -n "s/^[[:space:]]*\"${VALID_CLAIM}\":[[:space:]]*\"\([^\"]*\)\".*/\1/p"
    )
    INVALID_SASL_USER="$(date +%s)-invalid-user"
    VALID_SCOPE=$(
        jwt-parse |
        sed -n 's/.*"scope": "\([^"]*\)".*/\1/p' |
        awk '{print $1}'
    )
    INVALID_SCOPE="$(date +%s)-invalid-scope"
    INVALID_SCOPE2="$(date +%s)-2-invalid-scope"

    sed \
    -e "s|@VALID_SASL_USER@|$VALID_SASL_USER|g" \
    -e "s|@INVALID_SASL_USER@|$INVALID_SASL_USER|g" \
    -e "s|@VALID_ISSUER@|$VALID_ISSUER|g" \
    -e "s|@INVALID_ISSUER@|$INVALID_ISSUER|g" \
    -e "s|@INVALID_ISSUER2@|$INVALID_ISSUER2|g" \
    -e "s|@ISSUER_PARENT@|$ISSUER_PARENT|g" \
    -e "s|@VALID_CLAIM@|$VALID_CLAIM|g" \
    -e "s|@INVALID_CLAIM@|$INVALID_CLAIM|g" \
    -e "s|@INVALID_CLAIM2@|$INVALID_CLAIM2|g" \
    -e "s|@VALID_SCOPE@|$VALID_SCOPE|g" \
    -e "s|@INVALID_SCOPE@|$INVALID_SCOPE|g" \
    -e "s|@INVALID_SCOPE2@|$INVALID_SCOPE2|g" \
    tests.table.in > "$TMP_TABLE"

    while IFS= read -r line
    do
        for token in \
            VALID_ISSUER \
            INVALID_ISSUER \
            INVALID_ISSUER2 \
            INVALID_SASL_USER \
            VALID_SCOPE \
            INVALID_SCOPE \
            INVALID_SCOPE2
        do
            line=$(expand_wildcard \
                "${!token}" "$line" "${token}_WILDCARD")
        done

        printf '%s\n' "$line"
    done < "$TMP_TABLE" > tests.table

    sed \
    -e "s|@VALID_SCOPE@|$VALID_SCOPE|g" \
    -e "s|@VALID_AUD@|$VALID_AUD|g" \
    -e "s|@VALID_CLAIM@|$VALID_CLAIM|g" \
    -e "s|@VALID_ISSUER@|$VALID_ISSUER|g" \
    -e "s|@INVALID_SASL_USER@|$INVALID_SASL_USER|g" \
    -e "s|@INVALID_SCOPE@|$INVALID_SCOPE|g" \
    -e "s|@INVALID_ISSUER@|$INVALID_ISSUER|g" \
    init_gfarm_sasl.conf.in > init_gfarm_sasl.conf

    rm -f "$TMP_TABLE"
else
    echo "[ERROR] XOAUTH2 mechanism is not enabled. \
Please check the configuration and try again."
    cleanup
    exit 1
fi

source ./host.sh

BACKUP_DIR=$(mktemp -d)

cleanup() {
    restore_gfarm_conf

    if grep -qx 'log_auth_verbose enable' ~/.gfarm2rc 2>/dev/null; then
        echo '[INFO] removing "log_auth_verbose enable" from ~/.gfarm2rc'
        sed -i '\~^log_auth_verbose enable$~d' ~/.gfarm2rc
    fi
}
trap cleanup EXIT

backup_gfarm_conf()
{
    LIBDIR=$(pkg-config --variable=libdir libsasl2)

    for host in "${HOSTS[@]}"
    do
        echo "[INFO] backup gfarm.conf from $host"

        scp -q \
            "$host:${LIBDIR}/sasl2/gfarm.conf" \
            "$BACKUP_DIR/${host}.conf"
    done
}

restore_gfarm_conf()
{
    LIBDIR=$(pkg-config --variable=libdir libsasl2)

    for host in "${HOSTS[@]}"
    do
        echo "[INFO] restore gfarm.conf to $host"

        scp -q \
            "$BACKUP_DIR/${host}.conf" \
            "$host:/tmp/gfarm.conf"

        ssh -n "$host" \
            "sudo cp /tmp/gfarm.conf ${LIBDIR}/sasl2/gfarm.conf"
    done

    gfservice restart-all

    rm -rf "$BACKUP_DIR"
}

PASS_COUNT=0
FAIL_COUNT=0
TOTAL_COUNT=0
FAIL_LIST=()

backup_gfarm_conf

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

echo
echo "logs are saved under: $LOG_ROOT/"
