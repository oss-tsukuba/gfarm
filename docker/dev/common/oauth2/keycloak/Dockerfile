FROM quay.io/keycloak/keycloak:19.0.3-legacy

USER root

RUN microdnf update -y && microdnf install -y jq && microdnf clean all

ADD oauth2/keycloak/setup.sh /setup.sh
ADD functions.sh /functions.sh

ENTRYPOINT []
CMD ["/sbin/init"]
