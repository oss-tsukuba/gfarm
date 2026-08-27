FROM opensuse/leap:16.0

ARG USERNAME=foo
ARG UID=1000
ARG GCT_REPO=https://download.opensuse.org/repositories/home:/frank_scheiner:/gct/16.0/home:frank_scheiner:gct.repo

RUN --mount=type=cache,target=/var/cache/zypp \
    zypper --non-interactive install -y \
    git wget rpm-build sudo shadow openssh gcc gcc-c++ make autoconf automake cmake libtool \
    libopenssl-devel sqlite3-devel postgresql postgresql-devel postgresql-server postgresql-server-devel \
    python3 python3-pip python3-docopt python3-schema python3-flake8 \
    ruby \
    fuse fuse-devel fuse3 fuse3-devel autofs libacl-devel \
    cyrus-sasl cyrus-sasl-devel cyrus-sasl-plain cyrus-sasl-gssapi \
    krb5-devel krb5-server krb5-client go jq libcurl-devel libuuid-devel \
    rsyslog systemd patch rsync hostname \
    gdb valgrind strace libtsan2 libasan8 libubsan1 \
 && zypper --non-interactive addrepo "$GCT_REPO" \
 && zypper --gpg-auto-import-keys --non-interactive refresh \
 && zypper --non-interactive install -y \
    globus-gssapi-gsi-devel globus-gss-assist-devel globus-gsi-sysconfig-devel \
    globus-gsi-cert-utils-devel globus-gsi-cert-utils-progs globus-gsi-proxy-core-devel \
    globus-gsi-credential-devel globus-gsi-callback-devel globus-common-devel \
    globus-simple-ca globus-proxy-utils scitokens-cpp-devel \
 && git clone https://github.com/gridcf/gct.git /tmp/gct \
 && cd /tmp/gct/myproxy/source \
 && autoreconf -fi && ./configure && make -j"$(nproc)" && make install \
 && echo /usr/local/lib > /etc/ld.so.conf.d/myproxy.conf && ldconfig \
 && rm -rf /tmp/gct

RUN systemctl enable sshd \
 && printf '%s\n' 'Host *' '    StrictHostKeyChecking no' \
    > /etc/ssh/ssh_config.d/ignore-host-key.conf \
 && printf '%s\n' 'PermitEmptyPasswords yes' 'UsePAM no' \
    > /etc/ssh/sshd_config.d/insecure.conf

RUN useradd -m -u "$UID" -s /bin/bash "$USERNAME" \
 && echo "$USERNAME ALL=(ALL:ALL) NOPASSWD: ALL" > "/etc/sudoers.d/$USERNAME" \
 && passwd -d "$USERNAME" \
 && chmod u+r /etc/shadow

RUN useradd -m _gfarmfs \
 && useradd -m _gfarmmd

RUN sed -i 's@^Defaults secure_path="/usr/sbin:@Defaults secure_path="/usr/local/bin:/usr/sbin:@' /usr/etc/sudoers

CMD ["/sbin/init"]
