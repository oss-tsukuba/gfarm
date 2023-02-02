FROM jboss/keycloak:15.0.2

USER root

RUN microdnf update -y && microdnf install -y jq && microdnf clean all

ADD setup.sh /setup.sh

