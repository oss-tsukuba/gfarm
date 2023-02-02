FROM rosyuku/ubuntu-rdp

RUN useradd -m user && \
    echo "user:user" | chpasswd && \
    echo "user ALL=(ALL) ALL" >> /etc/sudoers
