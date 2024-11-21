#!/usr/bin/env python3

import sys
import json
import time
from pprint import pformat as pf

import msgspec

from keycloak import KeycloakAdmin
from keycloak.exceptions import KeycloakError


class KeycloakConfig(msgspec.Struct):
    keycloak_url: str
    admin_username: str
    admin_password: str
    realm_master: str
    realm: str
    client_secret: str
    jwt_server_urls: list[str]
    verify_cert: bool


with open(sys.argv[1], 'rb') as f:
    yaml_str = f.read()
config = msgspec.yaml.decode(yaml_str, type=KeycloakConfig)

# print(pf(config))

KEYCLOAK_URL = config.keycloak_url
ADMIN_USERNAME = config.admin_username
ADMIN_PASSWORD = config.admin_password
REALM_MASTER = config.realm_master
REALM = config.realm
CLIENT_SECRET = config.client_secret
JWT_SERVER_URLS = config.jwt_server_urls
VERIFY_CERT = config.verify_cert

DEFAULT_SCOPES = ['scitokens', 'openid', 'offline_access', 'hpci']
OPTIONAL_SCOPES = []


def E(*args):
    print('Error:', *args, file=sys.stderr)


try:
    kapi = KeycloakAdmin(
        server_url=KEYCLOAK_URL,
        username=ADMIN_USERNAME,
        password=ADMIN_PASSWORD,
        realm_name=REALM_MASTER,
        user_realm_name=REALM_MASTER,
        verify=VERIFY_CERT,
    )
except KeycloakError as e:
    E(e.error_message)
    sys.exit(1)

while True:
    try:
        realms = kapi.get_realms()
        break
    except Exception:
        print("waiting for keycloak startup")
        time.sleep(1)

# REALM
realm_dict = {realm['realm']: realm for realm in realms}

realm = realm_dict.get(REALM)
if realm:
    print(f'create realm "{REALM}": already exists')
else:
    new_realm = {
        'realm': REALM,
        'enabled': True
    }
    kapi.create_realm(new_realm)
    realm = kapi.get_realm(REALM)

print(pf(realm))

keycloak_version = realm.get('keycloakVersion')
print(f"keycloakVersion={str(keycloak_version)}")

try:
    kapi = KeycloakAdmin(
        server_url=KEYCLOAK_URL,
        username=ADMIN_USERNAME,
        password=ADMIN_PASSWORD,
        realm_name=REALM,
        user_realm_name=REALM_MASTER,
        verify=VERIFY_CERT,
    )
except KeycloakError as e:
    E(e.error_message)
    sys.exit(1)

# PUT /admin/realms/{realm}/events/config
event_config = '''
{
  "eventsEnabled" : true,
  "eventsListeners" : [ "jboss-logging" ],
  "enabledEventTypes" : [ "SEND_RESET_PASSWORD",
  "UPDATE_CONSENT_ERROR", "GRANT_CONSENT", "VERIFY_PROFILE_ERROR",
  "REMOVE_TOTP", "REVOKE_GRANT", "UPDATE_TOTP", "LOGIN_ERROR",
  "CLIENT_LOGIN", "RESET_PASSWORD_ERROR", "IMPERSONATE_ERROR",
  "CODE_TO_TOKEN_ERROR", "CUSTOM_REQUIRED_ACTION",
  "OAUTH2_DEVICE_CODE_TO_TOKEN_ERROR", "RESTART_AUTHENTICATION",
  "IMPERSONATE", "UPDATE_PROFILE_ERROR", "LOGIN",
  "OAUTH2_DEVICE_VERIFY_USER_CODE", "UPDATE_PASSWORD_ERROR",
  "CLIENT_INITIATED_ACCOUNT_LINKING", "TOKEN_EXCHANGE",
  "AUTHREQID_TO_TOKEN", "LOGOUT", "REGISTER", "DELETE_ACCOUNT_ERROR",
  "CLIENT_REGISTER", "IDENTITY_PROVIDER_LINK_ACCOUNT",
  "DELETE_ACCOUNT", "UPDATE_PASSWORD", "CLIENT_DELETE",
  "FEDERATED_IDENTITY_LINK_ERROR", "IDENTITY_PROVIDER_FIRST_LOGIN",
  "CLIENT_DELETE_ERROR", "VERIFY_EMAIL", "CLIENT_LOGIN_ERROR",
  "RESTART_AUTHENTICATION_ERROR", "EXECUTE_ACTIONS",
  "REMOVE_FEDERATED_IDENTITY_ERROR", "TOKEN_EXCHANGE_ERROR",
  "PERMISSION_TOKEN", "SEND_IDENTITY_PROVIDER_LINK_ERROR",
  "EXECUTE_ACTION_TOKEN_ERROR", "SEND_VERIFY_EMAIL",
  "OAUTH2_DEVICE_AUTH", "EXECUTE_ACTIONS_ERROR",
  "REMOVE_FEDERATED_IDENTITY", "OAUTH2_DEVICE_CODE_TO_TOKEN",
  "IDENTITY_PROVIDER_POST_LOGIN",
  "IDENTITY_PROVIDER_LINK_ACCOUNT_ERROR",
  "OAUTH2_DEVICE_VERIFY_USER_CODE_ERROR", "UPDATE_EMAIL",
  "REGISTER_ERROR", "REVOKE_GRANT_ERROR", "EXECUTE_ACTION_TOKEN",
  "LOGOUT_ERROR", "UPDATE_EMAIL_ERROR", "CLIENT_UPDATE_ERROR",
  "AUTHREQID_TO_TOKEN_ERROR", "UPDATE_PROFILE",
  "CLIENT_REGISTER_ERROR", "FEDERATED_IDENTITY_LINK",
  "SEND_IDENTITY_PROVIDER_LINK", "SEND_VERIFY_EMAIL_ERROR",
  "RESET_PASSWORD", "CLIENT_INITIATED_ACCOUNT_LINKING_ERROR",
  "OAUTH2_DEVICE_AUTH_ERROR", "UPDATE_CONSENT", "REMOVE_TOTP_ERROR",
  "VERIFY_EMAIL_ERROR", "SEND_RESET_PASSWORD_ERROR", "CLIENT_UPDATE",
  "CUSTOM_REQUIRED_ACTION_ERROR",
  "IDENTITY_PROVIDER_POST_LOGIN_ERROR", "UPDATE_TOTP_ERROR",
  "CODE_TO_TOKEN", "VERIFY_PROFILE", "GRANT_CONSENT_ERROR",
  "IDENTITY_PROVIDER_FIRST_LOGIN_ERROR" ],

 "adminEventsEnabled" : true,
 "adminEventsDetailsEnabled" : false
}
'''
kapi.set_events(json.loads(event_config))
print('update events/config')

realm_config = '''
{
  "enabled": true,
  "loginWithEmailAllowed": false,
  "defaultSignatureAlgorithm": "ES256",
  "ssoSessionIdleTimeout": 21600,
  "offlineSessionMaxLifespanEnabled": true,
  "offlineSessionMaxLifespan": 31536000,
  "accessTokenLifespan": 600,
  "oauth2DeviceCodeLifespan": 300,
  "oauth2DevicePollingInterval": 10
}
'''
kapi.update_realm(REALM, json.loads(realm_config))
print('update REALM')

realm = kapi.get_realm(REALM)
realm_id = realm['id']
print(f'realm_id={realm_id}')

keys = kapi.get_keys()
algorithms = []
for key in keys['keys']:
    algorithms.append(key['algorithm'])
ES256 = 'ES256'
if ES256 in algorithms:
    print(f'create key "{ES256}": already exist')
else:
    component = {
        "name": "ecdsa-generated",
        "providerId": "ecdsa-generated",
        "providerType": "org.keycloak.keys.KeyProvider",
        "parentId": realm_id,
        "config": {
            "priority": ["200"],
            "enabled": ["true"],
            "active": ["true"]
        }
    }
    kapi.create_component(component)
    print(f'create key "{ES256}"')


# client scope
def create_client_scope(name):
    scope = {
        "alias": name,
        "name": name,
        "protocol": "openid-connect"
    }
    kapi.create_client_scope(scope, skip_exists=True)


def create_mapper(scope_name, mapper_name, payload):
    scope = kapi.get_client_scope_by_name(scope_name)
    print(f'client scope {scope}')
    scope_id = scope['id']
    print(f'client scope id {scope_id}')
    mappers = kapi.get_mappers_from_client_scope(scope_id)
    print('mappers: ' + pf(mappers))
    mapper_dict = {}
    for mapper in mappers:
        mapper_dict[mapper['name']] = mapper
    mapper = mapper_dict.get(mapper_name, None)
    if mapper is None:
        kapi.add_mapper_to_client_scope(scope_id, payload)
    else:
        mapper_id = mapper['id']
        print(f'mapper id {mapper_id} to update')
        # for Uncaught server error: java.lang.NullPointerException
        payload['id'] = mapper_id
        kapi.update_mapper_in_client_scope(scope_id, mapper_id, payload)


scope_openid = 'openid'
mapper_nbf_name = 'nbf'
mapper_nbf = {
    "name": mapper_nbf_name,
    "protocol": "openid-connect",
    "consentRequired": False,
    "protocolMapper": "oidc-hardcoded-claim-mapper",
    "config": {
        "claim.name": mapper_nbf_name,
        "claim.value": 0,
        "jsonType.label": "int",
        "userinfo.token.claim": False,
        "id.token.claim": False,
        "access.token.claim": True,
        "access.tokenResponse.claim": False,
    }
}

scope_scitokens = 'scitokens'
mapper_ver_name = 'ver'
mapper_ver = {
    "name": mapper_ver_name,
    "protocol": "openid-connect",
    "consentRequired": False,
    "protocolMapper": "oidc-hardcoded-claim-mapper",
    "config": {
        "claim.name": mapper_ver_name,
        "claim.value": "scitokens:2.0",
        "jsonType.label": "String",
        "userinfo.token.claim": True,
        "id.token.claim": False,
        "access.token.claim": True,
        "access.tokenResponse.claim": False,
    }
}

scope_hpci = 'hpci'
mapper_hpciver_name = 'hpci.ver'
mapper_hpciver = {
    "name": mapper_hpciver_name,
    "protocol": "openid-connect",
    "consentRequired": False,
    "protocolMapper": "oidc-hardcoded-claim-mapper",
    "config": {
        "claim.name": "hpci\\.ver",
        "claim.value": "1.0",
        "jsonType.label": "String",
        "userinfo.token.claim": True,
        "id.token.claim": False,
        "access.token.claim": True,
        "access.tokenResponse.claim": False,
    }
}

mapper_hpciid_name = 'hpci.id'
mapper_hpciid = {
    "name": mapper_hpciid_name,
    "protocol": "openid-connect",
    "consentRequired": False,
    "protocolMapper": "oidc-usermodel-attribute-mapper",
    "config": {
        "claim.name": "hpci\\.id",
        "user.attribute": "hpci.id",
        "jsonType.label": "String",
        "userinfo.token.claim": False,
        "id.token.claim": False,
        "access.token.claim": True,
    }
}

mapper_localacc_name = 'local-accounts'
mapper_localacc = {
    "name": mapper_localacc_name,
    "protocol": "openid-connect",
    "consentRequired": False,
    "protocolMapper": "oidc-usermodel-attribute-mapper",
    "config": {
        "claim.name": "local-accounts",
        "user.attribute": "local-accounts",
        "jsonType.label": "JSON",
        "userinfo.token.claim": True,
        "id.token.claim": False,
        "access.token.claim": False,
    }
}

mapper_hpciglobal_name = 'hpci-global'
mapper_hpciglobal = {
    "name": mapper_hpciglobal_name,
    "protocol": "openid-connect",
    "consentRequired": False,
    "protocolMapper": "oidc-audience-mapper",
    "config": {
        "id.token.claim": False,
        "access.token.claim": True,
        "included.custom.audience": "hpci",
    }
}

create_client_scope(scope_openid)
create_mapper(scope_openid, mapper_nbf_name, mapper_nbf)
create_client_scope(scope_scitokens)
create_mapper(scope_scitokens, mapper_ver_name, mapper_ver)
create_client_scope(scope_hpci)
create_mapper(scope_hpci, mapper_hpciver_name, mapper_hpciver)
create_mapper(scope_hpci, mapper_hpciid_name, mapper_hpciid)
create_mapper(scope_hpci, mapper_localacc_name, mapper_localacc)
create_mapper(scope_hpci, mapper_hpciglobal_name, mapper_hpciglobal)

# client
client_public = {
    "clientId": "hpci-pub",
    "directAccessGrantsEnabled": True,
    "publicClient": True,
    "redirectUris": ["http://localhost:8080/"],
    "attributes": {
        "oauth2.device.authorization.grant.enabled": True,
        "client.offline.session.idle.timeout": 86400,
        "client.offline.session.max.lifespan": 604800,
    }
}

client_private = {
    "clientId": "hpci-jwt-server",
    "directAccessGrantsEnabled": True,
    "publicClient": False,
    "redirectUris": JWT_SERVER_URLS,
    "attributes": {
        "oauth2.device.authorization.grant.enabled": True,
        "client.offline.session.idle.timeout": 86400,
        "client.offline.session.max.lifespan": 31536000,
    }
}

client_id_id_public = kapi.get_client_id(client_public['clientId'])
client_id_id_private = kapi.get_client_id(client_private['clientId'])

if client_id_id_public is None:
    kapi.create_client(client_public, skip_exists=True)
    client_id_id_public = kapi.get_client_id(client_public['clientId'])
else:
    kapi.update_client(client_id_id_public, client_public)

if client_id_id_private is None:
    kapi.create_client(client_private, skip_exists=True)
    client_id_id_private = kapi.get_client_id(client_private['clientId'])
else:
    kapi.update_client(client_id_id_private, client_private)


def delete_unnecessary_scope(client_id_id, target, scope_names):
    if target == 'default':
        scopes = kapi.get_client_default_client_scopes(client_id_id)
    elif target == 'optional':
        scopes = kapi.get_client_optional_client_scopes(client_id_id)
    # print(f'DEBUG target={target} scopes=' + pf(scopes))
    scope_name_set = set(scope_names)
    exist_names = []
    for scope in scopes:
        name = scope['name']
        if name in scope_name_set:
            # print(f'DEBUG leave: target={target} name={name} scope='
            #       + pf(scope))
            exist_names.append(name)
        else:
            # print(f'DEBUG delete: target={target} name={name} scope='
            #       + pf(scope))
            if target == 'default':
                kapi.delete_client_default_client_scope(
                    client_id_id, scope['id'])
            elif target == 'optional':
                kapi.delete_client_optional_client_scope(
                    client_id_id, scope['id'])
    return exist_names


def add_scope(client_id_id, target, add_names):
    for scope_name in add_names:
        scope = kapi.get_client_scope_by_name(scope_name)
        # print(f'DEBUG add: target={target} scope[{scope_name}]=' + pf(scope))
        scope_id = scope['id']
        payload = {
            "realm": REALM,
            "client": client_id_id,
            "clientScopeId": scope_id,
        }
        if target == 'default':
            kapi.add_client_default_client_scope(
                client_id_id, scope_id, payload)
        elif target == 'optional':
            kapi.add_client_optional_client_scope(
                client_id_id, scope_id, payload)


def update_client_scopes(client_id_id, default_names, optional_names):
    print(f'------------- update client scopes: {client_id_id}')
    default_exist_names = delete_unnecessary_scope(
        client_id_id, 'default', default_names)
    optional_exist_names = delete_unnecessary_scope(
        client_id_id, 'optional', optional_names)
    default_add_names = list(set(default_names) - set(default_exist_names))
    optional_add_names = list(set(optional_names) - set(optional_exist_names))
    add_scope(client_id_id, 'default', default_add_names)
    add_scope(client_id_id, 'optional', optional_add_names)


# d = kapi.get_client_default_client_scopes(client_id_id_public)
# print(pf(d))
# opt = kapi.get_client_optional_client_scopes(client_id_id_public)
# print(pf(opt))

update_client_scopes(client_id_id_public, DEFAULT_SCOPES, OPTIONAL_SCOPES)
update_client_scopes(client_id_id_private, DEFAULT_SCOPES, OPTIONAL_SCOPES)

# secret
private_secret = {
    "secret": CLIENT_SECRET,
}
kapi.update_client(client_id_id_private, private_secret)


# authentication/flows
def update_execution(flow_alias, display_name, requirement):
    flows = kapi.get_authentication_flow_executions(flow_alias)
    for flow in flows:
        if flow['displayName'] == display_name:
            break
    flow_exec_update = {
        "id": flow['id'],
        "requirement": requirement
    }
    kapi.update_authentication_flow_executions(flow_exec_update, flow_alias)


def create_authentication_flow_execution(flow_exec, flow_alias, display_name):
    flows = kapi.get_authentication_flow_executions(flow_alias)
    for flow in flows:
        if flow['displayName'] == display_name:
            # exist
            print('create_authentication_flow_execution'
                  f' display_name={display_name}: already exists')
            return
    kapi.create_authentication_flow_execution(flow_exec, flow_alias)


flow_alias_totp = "TOTP"
flow_display_name_totp = "OTP Form"
flow_description_totp = "TOTP after SAML IdP login"
provider_id_otp = "auth-otp-form"
flow_totp = {
    "alias": flow_alias_totp,
    "description": flow_description_totp,
    "providerId": "basic-flow",
    "topLevel": True,
    "builtIn": False,
}
flow_exec_totp = {
    "provider": provider_id_otp,
}

kapi.create_authentication_flow(flow_totp, skip_exists=True)
create_authentication_flow_execution(
    flow_exec_totp, flow_alias_totp, flow_display_name_totp)
update_execution(flow_alias_totp, flow_display_name_totp, "REQUIRED")

flow_alias_fbl = "first%20broker%20login"
flow_display_name_fbl_review = "Review Profile"
flow_display_name_fbl_create = "User creation or linking"
update_execution(flow_alias_fbl, flow_display_name_fbl_review, "DISABLED")
update_execution(flow_alias_fbl, flow_display_name_fbl_create, "DISABLED")


# User
username = "user1"
email = "user1@example.org"
firstName = 'FirstName1'
lastName = 'LastName1'
password = "PASSWORD"

userid = kapi.create_user(
    {
        'username': username,
        'enabled': True,
    }, exist_ok=True)
print(f'create user: username={username}, userid={userid}')

userid = kapi.get_user_id(username)
localacc_str = json.dumps(
    {"host1.example.com": "user1000", "host2.example.org": "user2000"})
kapi.update_user(
    userid,
    {
        'email': email,
        'emailVerified': True,
        'attributes': {
            'firstName': firstName,
            'lastName': lastName,
            'hpci.id': username,
            'local-accounts': localacc_str,
        }
    })

kapi.set_user_password(userid, password, temporary=False)

user = kapi.get_user(userid)
print(pf(user))


print('### keycloak-config.py: DONE ###')
