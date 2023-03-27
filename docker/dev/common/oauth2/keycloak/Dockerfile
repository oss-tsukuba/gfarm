FROM jboss/keycloak:15.0.2

USER root

RUN microdnf update -y && microdnf install -y jq && microdnf clean all

COPY oauth2/keycloak/standalone-ha.xml /opt/jboss/keycloak/standalone/configuration/standalone-ha.xml
ADD oauth2/keycloak/setup.sh /setup.sh
ADD functions.sh /functions.sh

