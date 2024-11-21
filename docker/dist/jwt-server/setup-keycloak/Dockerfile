FROM ubuntu:24.04

RUN apt-get update \
  && apt-get install -y python3-pip python3-venv \
  && VENV_DIR=/venv \
  && python3 -m venv $VENV_DIR \
  && . ${VENV_DIR}/bin/activate \
  && pip3 install python-keycloak \
  && pip3 install msgspec[yaml]

COPY ./entrypoint.sh /entrypoint.sh

# debug tools
#RUN apt-get update && apt-get install -y iputils-ping

ENTRYPOINT ["/bin/sh", "/entrypoint.sh"]
#CMD ["python3", "/keycloak-setup/keycloak-config.py", "/keycloak-setup/keycloak-config.yaml"]
CMD ["true"]
