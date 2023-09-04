FROM ubuntu

ARG DEBIAN_FRONTEND=noninteractive
ARG TZ=Asia/Tokyo
ENV TZ=${TZ}

ARG USERNAME=foo
ARG UID=1000

RUN apt-get update \
 && apt-get -y upgrade \
 && apt-get -y install \
    git wget sudo vim systemctl \
    gcc make \
    libssl-dev libpq-dev \
    libglobus-gssapi-gsi-dev pkgconf \
    libibverbs-dev \
    postgresql postgresql-client \
    python3 ruby \
    xsltproc docbook-xsl \
    python3-docopt python3-schema python3-tqdm flake8 \
    fuse libfuse-dev libacl1-dev \
    gdb valgrind rsyslog \
    myproxy globus-simple-ca \
    libkrb5-dev \
    libsasl2-dev sasl2-bin libscitokens-dev \
    golang jq pandoc curl

RUN \
  # sshd
  apt-get -y install --no-install-recommends \
    openssh-server \
  # sshd_config
  && printf '%s\n' \
    'PasswordAuthentication yes' \
    'PermitEmptyPasswords yes' \
    'UsePAM no' \
    > /etc/ssh/sshd_config.d/auth.conf \
  # ssh_config
  && printf '%s\n' \
    'Host *' \
    '    StrictHostKeyChecking no' \
    > /etc/ssh/ssh_config.d/ignore-host-key.conf

RUN useradd -m -u $UID -s /bin/bash $USERNAME \
 && echo "$USERNAME ALL=(ALL:ALL) NOPASSWD: ALL" >> /etc/sudoers.d/$USERNAME \
 # delete passwd
 && passwd -d $USERNAME

RUN mv /usr/bin/grid-cert-request /usr/bin/grid-cert-request.orig \
 && sed 's/:\/var\/adm\/wtmp:\/var\/log\/messages//' \
      /usr/bin/grid-cert-request.orig > /usr/bin/grid-cert-request \
 && chmod +x /usr/bin/grid-cert-request

RUN useradd -m _gfarmfs \
 && useradd -m _gfarmmd

CMD service ssh start && systemctl start rsyslog && sleep infinity
