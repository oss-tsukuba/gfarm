#!/usr/bin/bash

set -eux

: $GFDOCKER_USERNAME_PREFIX
: $GFDOCKER_TENANTNAME_PREFIX
: $GFDOCKER_NUM_USERS
: $GFDOCKER_NUM_TENANTS
: $GFDOCKER_SASL_HPCI_SECET

BASEDIR=$(dirname $(realpath $0))
FUNCTIONS=${BASEDIR}/functions.sh
. ${FUNCTIONS}

keytool -importkeystore -srckeystore /mnt/jwt-keycloak/jwt-keycloak.p12 \
  -srcstoretype PKCS12 -srcstorepass PASSWORD \
  -destkeystore /opt/jboss/keycloak/standalone/configuration/keycloak.jks \
  -deststoretype JKS -deststorepass PASSWORD -destkeypass PASSWORD

ignore() {
    echo 1>&2 "ERROR IGNORED"
    # true
}

get_code() {
    # NOTE: -k: insecure
    curl -s -k --noproxy '*' -w '%{http_code}' "$1" -o /dev/null
}

wait_for_keycloak_to_become_ready() {
    URL="$1"
    EXPECT_CODE='^[23]0.*$'
    while :; do
        if CODE=$(get_code "$URL"); then
            if [[ "$CODE" =~ ${EXPECT_CODE} ]]; then
                break
            fi
        fi
        sleep 1
        echo "waiting for keycloak startup"
    done
}


KEYCLOAK_HOME=/opt/jboss/keycloak
KEYCLOAK_REALM=hpci
KEYCLOAK_ADMIN_REALM=master
MY_KEYCLOAK_SERVER=http://localhost:8080/auth
KEYCLOAK_USER=admin
KEYCLOAK_PASSWORD=admin
CLIENT_ID_PUBLIC="hpci-pub"
CLIENT_ID_CONFIDENTIAL="hpci-jwt-server"
HPCI_SECRET=${GFDOCKER_SASL_HPCI_SECET}
GF_PASSWORD=PASSWORD

BINDIR=${KEYCLOAK_HOME}/bin
KCADM=${BINDIR}/kcadm.sh

REALM=${KEYCLOAK_REALM}
ADMIN_REALM=${KEYCLOAK_ADMIN_REALM}

wait_for_keycloak_to_become_ready ${MY_KEYCLOAK_SERVER}

### login

${KCADM} config credentials \
	 --server ${MY_KEYCLOAK_SERVER} \
	 --realm ${ADMIN_REALM} \
	 --user ${KEYCLOAK_USER} \
	 --password ${KEYCLOAK_PASSWORD}

### create realm

${KCADM} create realms \
	 -s realm=$REALM || ignore

### set up realm

${KCADM} update realms/${REALM} \
	 -s enabled=true \
	 -s loginWithEmailAllowed=false \
	 -s defaultSignatureAlgorithm=ES256 \
	 -s ssoSessionIdleTimeout=21600 \
	 -s offlineSessionMaxLifespanEnabled=true \
	 -s offlineSessionMaxLifespan=604800 \
	 -s accessTokenLifespan=600 \
	 -s oauth2DeviceCodeLifespan=300 \
	 -s oauth2DevicePollingInterval=10

### maintain current values

get_clientId_id()
{
    local NAME="$1"
    ${KCADM} get clients -r ${REALM} | \
	jq -r '.[] | select(.clientId == "'${NAME}'") | .id'
}

get_secret()
{
    local CLIENT_ID_NAME="$1"
    local CLIENT_ID_ID
    CLIENT_ID_ID=$(get_clientId_id "$CLIENT_ID_NAME" || ignore)
    if [ -z "$CLIENT_ID_ID" ]; then
        # may not exist, not error
        return
    fi
    ${KCADM} get clients/${CLIENT_ID_ID}/client-secret -r ${REALM} | \
	jq -r ".value"
}

CLIENT_ID_CONFIDENTIAL_SECRET=$(get_secret "$CLIENT_ID_CONFIDENTIAL" || ignore)

### create key

RECREATE_KEY=0

REALM_ID=$(${KCADM} get realms/${REALM} --fields id --format csv --noquotes)

KEY_PROVIDER_ID=$(
    ${KCADM} get keys -r ${REALM} --fields 'keys(*)' | \
	jq -r '.keys[] | select(.algorithm == "ES256") | .providerId') || ignore

if [ -n "${KEY_PROVIDER_ID}" -a ${RECREATE_KEY} -eq 1 ]; then
    ${KCADM} delete components/${KEY_PROVIDER_ID} -r ${REALM}
    KEY_PROVIDER_ID=""
fi
if [ -z "${KEY_PROVIDER_ID}" ]; then
    ${KCADM} create components -r ${REALM} \
	 -s name=ecdsa-generated \
	 -s providerId=ecdsa-generated \
	 -s providerType=org.keycloak.keys.KeyProvider \
	 -s parentId=${REALM_ID} \
	 -s 'config.priority=["200"]' \
	 -s 'config.enabled=["true"]' \
	 -s 'config.active=["true"]'
fi

### client scopes

get_client_scope_id()
{
    ${KCADM} get client-scopes -r ${REALM} | \
	jq -r '.[] | select(.name == "'"${1}"'") | .id'
}

get_mapper_id()
{
    local CLIENT_SCOPE_ID="$1"
    local MAPPER_NAME="$2"
    ${KCADM} get client-scopes/${CLIENT_SCOPE_ID} -r ${REALM} | \
	jq -r '.protocolMappers[] | select(.name == "'${MAPPER_NAME}'") | .id'
}

RECREATE_CLIENT_SCOPE=0

create_client_scope()
{
    local SCOPE_NAME="$1"
    local SCOPE_ID

    SCOPE_ID=$(get_client_scope_id "$SCOPE_NAME" || ignore)

    if [ -n "${SCOPE_ID}" -a ${RECREATE_CLIENT_SCOPE} -eq 1 ]; then
        ${KCADM} delete client-scopes/${SCOPE_ID} -r ${REALM}
        SCOPE_ID=""
    fi
    if [ -z "${SCOPE_ID}" ]; then
        ${KCADM} create client-scopes -r ${REALM} \
	     -s alias=${SCOPE_NAME} \
	     -s name=${SCOPE_NAME} \
	     -s protocol=openid-connect
    else
        ${KCADM} update client-scopes/${SCOPE_ID} -r ${REALM} \
	     -s alias=${SCOPE_NAME} \
	     -s name=${SCOPE_NAME} \
	     -s protocol=openid-connect
    fi
}

RECREATE_MAPPER=0

create_mapper()
{
    local SCOPE_NAME="$1"
    local MAPPER_NAME="$2"
    shift
    shift

    local SCOPE_ID
    SCOPE_ID=$(get_client_scope_id "$SCOPE_NAME" || ignore)
    if [ -z "$SCOPE_ID" ]; then
        exit 1
    fi

    local MAPPER_ID
    MAPPER_ID=$(get_mapper_id "${SCOPE_ID}" "${MAPPER_NAME}" || ignore)

    if [ -n "${MAPPER_ID}" -a ${RECREATE_MAPPER} -eq 1 ]; then
	${KCADM} delete \
		 client-scopes/${SCOPE_ID}/protocol-mappers/models/${MAPPER_ID} \
		 -r ${REALM}
	MAPPER_ID=""
    fi
    if [ -z "${MAPPER_ID}" ]; then
        ${KCADM} create client-scopes/${SCOPE_ID}/protocol-mappers/models \
	     -r ${REALM} \
	     -s name=${MAPPER_NAME} \
	     -s protocol=openid-connect \
	     -s 'consentRequired="false"' \
	     "$@"
    else
        ${KCADM} update \
		 client-scopes/${SCOPE_ID}/protocol-mappers/models/${MAPPER_ID} \
	     -r ${REALM} \
	     -s name=${MAPPER_NAME} \
	     -s protocol=openid-connect \
	     -s 'consentRequired="false"' \
	     "$@"
    fi
}

SCOPE_NAME="openid"
create_client_scope $SCOPE_NAME
MAPPER_NAME="nbf"

create_mapper $SCOPE_NAME $MAPPER_NAME \
		-s protocolMapper=oidc-hardcoded-claim-mapper \
		-s 'config."claim.name"="'${MAPPER_NAME}'"' \
		-s 'config."claim.value"="0"' \
		-s 'config."jsonType.label"="int"' \
		-s 'config."userinfo.token.claim"="true"' \
		-s 'config."id.token.claim"="false"' \
		-s 'config."access.token.claim"="true"' \
		-s 'config."access.tokenResponse.claim"="false"'

SCOPE_NAME="scitokens"
create_client_scope $SCOPE_NAME
MAPPER_NAME="ver"
create_mapper $SCOPE_NAME $MAPPER_NAME \
		-s protocolMapper=oidc-hardcoded-claim-mapper \
		-s 'config."claim.name"="'${MAPPER_NAME}'"' \
		-s 'config."claim.value"="scitokens:2.0"' \
		-s 'config."jsonType.label"="String"' \
		-s 'config."userinfo.token.claim"="true"' \
		-s 'config."id.token.claim"="false"' \
		-s 'config."access.token.claim"="true"' \
		-s 'config."access.tokenResponse.claim"="false"'

SCOPE_NAME="hpci"
create_client_scope $SCOPE_NAME
MAPPER_NAME="hpci.ver"
create_mapper $SCOPE_NAME $MAPPER_NAME \
		-s protocolMapper=oidc-hardcoded-claim-mapper \
		-s 'config."claim.name"="hpci\.ver"' \
		-s 'config."claim.value"="1.0"' \
		-s 'config."jsonType.label"="String"' \
		-s 'config."userinfo.token.claim"="true"' \
		-s 'config."id.token.claim"="false"' \
		-s 'config."access.token.claim"="true"' \
		-s 'config."access.tokenResponse.claim"="false"'

MAPPER_NAME="hpci.id"
create_mapper $SCOPE_NAME $MAPPER_NAME \
		-s protocolMapper=oidc-usermodel-attribute-mapper \
		-s 'config."claim.name"="hpci\.id"' \
		-s 'config."user.attribute"="hpci.id"' \
		-s 'config."claim.value"="1.0"' \
		-s 'config."jsonType.label"="String"' \
		-s 'config."userinfo.token.claim"="true"' \
		-s 'config."id.token.claim"="false"' \
		-s 'config."access.token.claim"="true"'

MAPPER_NAME="hpci-global"
create_mapper $SCOPE_NAME $MAPPER_NAME \
		-s protocolMapper=oidc-audience-mapper \
		-s 'config."id.token.claim"="false"' \
		-s 'config."access.token.claim"="true"' \
		-s 'config."included.custom.audience"="hpci"'

# print to check
${KCADM} get client-scopes -r ${REALM}

### client

RECREATE_CLIENT_ID=0

create_client_id()
{
    local CLIENT_ID_NAME="$1"
    shift

    local CLIENT_ID_ID
    CLIENT_ID_ID=$(get_clientId_id "$CLIENT_ID_NAME" || ignore)
    if [ -n "${CLIENT_ID_ID}" -a ${RECREATE_CLIENT_ID} -eq 1 ]; then
        ${KCADM} delete clients/${CLIENT_ID_ID} -r ${REALM}
        CLIENT_ID_ID=""
    fi
    if [ -z "${CLIENT_ID_ID}" ]; then
        ${KCADM} create clients -r ${REALM} \
		 -s clientId=${CLIENT_ID_NAME} \
		 -s directAccessGrantsEnabled=true \
		 -s 'attributes."oauth2.device.authorization.grant.enabled"="true"' \
		 "$@"
    else
	${KCADM} update clients/${CLIENT_ID_ID} -r ${REALM} \
		 -s clientId=${CLIENT_ID_NAME} \
		 -s directAccessGrantsEnabled=true \
		 -s 'attributes."oauth2.device.authorization.grant.enabled"="true"' \
		 "$@"
    fi
}

update_secret()
{
    local CLIENT_ID_NAME="$1"
    local OLD_SECRET="$2"
    shift

    local CLIENT_ID_ID
    CLIENT_ID_ID=$(get_clientId_id "$CLIENT_ID_NAME" || ignore)
    if [ -z "$CLIENT_ID_ID" ]; then
        exit 1
    fi

    local SECRET=
    if [ -n "$OLD_SECRET" ]; then
        SECRET="$OLD_SECRET"
    else
        SECRET=$HPCI_SECRET
    fi
    ${KCADM} update clients/${CLIENT_ID_ID} -r ${REALM} \
	     -s "secret=${SECRET}"
}

delete_unnecessary_scope() {
    local TARGET="$1"
    local CLIENT_ID_ID="$2"
    local SCOPES="$3"

    local client_scope_id
    local scope1
    local scope2
    local found
    local exist=""
    for scope1 in $(${KCADM} get clients/${CLIENT_ID_ID}/${TARGET}-client-scopes -r ${REALM} \
			| jq -r '.[] | .name'); do
        found=0
        for scope2 in ${SCOPES}; do
            if [ "${scope1}" = "${scope2}" ]; then
                found=1
                exist="${exist} ${scope1}"
                break
            fi
        done
        if [ ${found} -eq 0 ]; then
            client_scope_id=$(get_client_scope_id ${scope1})
            ${KCADM} delete \
		     clients/${CLIENT_ID_ID}/${TARGET}-client-scopes/${client_scope_id} \
		     -r ${REALM} > /dev/null
        fi
    done
    echo "${exist}"
}

add_scope() {
    local TARGET="$1"
    local CLIENT_ID_ID="$2"
    local SCOPES="$3"
    local exist="$4"

    local client_scope_id
    local scope1
    local scope2
    local foundw
    for scope2 in ${SCOPES}; do
        found=0
        for scope1 in ${exist}; do
            if [ "${scope1}" = "${scope2}" ]; then
                found=1
                break
            fi
        done
        if [ ${found} -eq 0 ]; then
            client_scope_id=$(get_client_scope_id ${scope2})
            ${KCADM} update \
		     clients/${CLIENT_ID_ID}/${TARGET}-client-scopes/${client_scope_id} \
		     -r ${REALM}
        fi
    done
}

update_client_scopes() {
    local CLIENT_ID_NAME="$1"
    local DEFAULT_SCOPES="$2"
    local OPTIONAL_SCOPES="$3"
    local CLIENT_ID_ID
    CLIENT_ID_ID=$(get_clientId_id "$CLIENT_ID_NAME" || ignore)
    if [ -z "$CLIENT_ID_ID" ]; then
        exit 1
    fi

    local exist_default
    local exist_optional

    exist_default=$(delete_unnecessary_scope default ${CLIENT_ID_ID} "${DEFAULT_SCOPES}")
    exist_optional=$(delete_unnecessary_scope optional ${CLIENT_ID_ID} "${OPTIONAL_SCOPES}")

    add_scope default ${CLIENT_ID_ID} "${DEFAULT_SCOPES}" "${exist_default}"
    add_scope optional ${CLIENT_ID_ID} "${OPTIONAL_SCOPES}" "${exist_optional}"
}

DEFAULT_SCOPES="scitokens openid offline_access hpci"
OPTIONAL_SCOPES=""

create_client_id "$CLIENT_ID_PUBLIC" \
  -s publicClient=true \
  -s 'redirectUris=["http://localhost:8080/"]' \
  -s 'attributes."client.offline.session.idle.timeout"="86400"'\
  -s 'attributes."client.offline.session.max.lifespan"="604800"'
update_client_scopes "${CLIENT_ID_PUBLIC}" "${DEFAULT_SCOPES}" "${OPTIONAL_SCOPES}"

create_client_id "$CLIENT_ID_CONFIDENTIAL" \
  -s publicClient=false \
  -s 'redirectUris=["https://jwt-server.test/*", "http://jwt-server.test/*", "https://jwt-server/*", "http://jwt-server/*"]' \
  -s 'attributes."client.offline.session.idle.timeout"="86400"'\
  -s 'attributes."client.offline.session.max.lifespan"="31536000"'
update_client_scopes "${CLIENT_ID_CONFIDENTIAL}" \
		     "${DEFAULT_SCOPES}" \
		     "${OPTIONAL_SCOPES}"

update_secret "$CLIENT_ID_CONFIDENTIAL" "$CLIENT_ID_CONFIDENTIAL_SECRET"


${KCADM} get clients -r ${REALM}

### authentication/flows

get_flow_execution_id()
{
    local FLOW_ALIAS="$1"
    local TARGET="$2"

    ${KCADM} get "authentication/flows/${FLOW_ALIAS}/executions" \
	     -r $REALM | \
	jq -r '.[] | select (.displayName == "'"${TARGET}"'") | .id'
}

change_execution()
{
    local FLOW_ALIAS="$1"
    local DISPLAY_NAME="$2"
    local REQUIREMENT="$3"
    local FLOW_EXECUTION_ID
    FLOW_EXECUTION_ID=$(get_flow_execution_id "$FLOW_ALIAS" "$DISPLAY_NAME" || ignore)


    if [ -z "$FLOW_EXECUTION_ID" ]; then
        exit 1
    fi
    ${KCADM} update "authentication/flows/${FLOW_ALIAS}/executions" -r $REALM \
        -f - <<EOF
{
  "id" : "${FLOW_EXECUTION_ID}",
  "requirement" : "${REQUIREMENT}"
}
EOF
}

FLOW_ALIAS="first%20broker%20login"
#PROVIDER_ID="idp-review-profile"
DISPLAY_NAME="Review Profile"
change_execution "$FLOW_ALIAS" "$DISPLAY_NAME" "DISABLED"

DISPLAY_NAME="User creation or linking"
change_execution "$FLOW_ALIAS" "$DISPLAY_NAME" "DISABLED"

${KCADM} get authentication/flows/${FLOW_ALIAS}/executions -r $REALM

### create user

get_user_id()
{
    local NAME="$1"
    ${KCADM} get users -r ${REALM} | \
	jq -r '.[] | select(.username == "'"${NAME}"'") | .id'
}

RECRETE_USER_ID=0

create_user()
{
    local USER="$1"
    local PASSWORD="$2"
    shift

    local USER_ID

    USER_ID=$(get_user_id "${USER}" || ignore)
    echo "USER_ID:"$USER_ID
    if [ -n "${USER_ID}" -a ${RECRETE_USER_ID} -eq 1 ]; then
        ${KCADM} delete users/${USER_ID} -r ${REALM}
        USER_ID=""
    fi

    ${BINDIR}/add-user-keycloak.sh -r $REALM \
      --user "${USER}" \
      --password "${PASSWORD}" || ignore
}

update_user()
{
    local USER="$1"
    local GFARM_USER="$2"
    shift
    shift

    local USER_ID

    USER_ID=$(get_user_id "${USER}" || ignore)
    ARG='attributes."hpci.id"='"${GFARM_USER}"
    ${KCADM} update users/${USER_ID} -r ${REALM} \
                 -s "${ARG}"
}

i=0
for t in $(seq 1 "$GFDOCKER_NUM_TENANTS"); do
 for u in $(seq 1 "$GFDOCKER_NUM_USERS"); do
  i=$((i + 1))
  create_user "${GFDOCKER_USERNAME_PREFIX}${i}" "${GF_PASSWORD}"
 done
done

${BINDIR}/jboss-cli.sh -c --command=reload

### login

${KCADM} config credentials \
	 --server ${MY_KEYCLOAK_SERVER} \
	 --realm ${ADMIN_REALM} \
	 --user ${KEYCLOAK_USER} \
	 --password ${KEYCLOAK_PASSWORD}

i=0
for t in $(seq 1 "$GFDOCKER_NUM_TENANTS"); do
 for u in $(seq 1 "$GFDOCKER_NUM_USERS"); do
  i=$((i + 1))
  guser="$(gfuser_from_index $t $u)"
  tenant_user_suffix="$(gftenant_user_suffix_from_index $t)"
  update_user "${GFDOCKER_USERNAME_PREFIX}${i}" "${guser}${tenant_user_suffix}"
 done
done
