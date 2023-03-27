#!/bin/sh

set -eux

CA_DIR=/root/simple_ca
HOST_SHARE_DIR=/mnt

# for desktop
sudo mkdir -p ${HOST_SHARE_DIR}/desktop
sudo cp ${CA_DIR}/cacert.pem ${HOST_SHARE_DIR}/desktop

# for jwt-tomcat
sudo mkdir -p ${HOST_SHARE_DIR}/jwt-tomcat
sudo cp ${CA_DIR}/cacert.pem ${HOST_SHARE_DIR}/jwt-tomcat

# for jwt-server HTTPS
sudo mkdir -p ${HOST_SHARE_DIR}/jwt-server
sudo cp /etc/grid-security/jwt-servercert.pem ${HOST_SHARE_DIR}/jwt-server/
sudo cp /etc/grid-security/jwt-serverkey.pem ${HOST_SHARE_DIR}/jwt-server/

# for jwt-keycloak HTTPS
sudo mkdir -p ${HOST_SHARE_DIR}/jwt-keycloak
sudo cp /etc/grid-security/jwt-keycloakcert.pem ${HOST_SHARE_DIR}/jwt-keycloak/
sudo cp /etc/grid-security/jwt-keycloakkey.pem ${HOST_SHARE_DIR}/jwt-keycloak/
sudo cp /etc/grid-security/jwt-keycloak.p12 ${HOST_SHARE_DIR}/jwt-keycloak/
