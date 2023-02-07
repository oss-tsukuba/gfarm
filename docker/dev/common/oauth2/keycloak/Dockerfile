FROM jboss/keycloak:15.0.2

USER root

RUN microdnf update -y && microdnf install -y jq && microdnf clean all

COPY standalone-ha.xml /opt/jboss/keycloak/standalone/configuration/standalone-ha.xml

ADD setup.sh /setup.sh

