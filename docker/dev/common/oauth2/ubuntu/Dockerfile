FROM rosyuku/ubuntu-rdp

RUN apt-get update && \
    apt-get install -y libnss3-tools && \
    useradd -m user && \
    echo "user:user" | chpasswd && \
    echo "user ALL=(ALL) ALL" >> /etc/sudoers

COPY ./install-ca-for-browser.sh /usr/local/bin/
