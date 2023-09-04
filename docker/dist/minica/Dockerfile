FROM ubuntu

ARG DEBIAN_FRONTEND=noninteractive
ARG TZ=Asia/Tokyo
ENV TZ=${TZ}

RUN apt-get update \
 && apt-get -y upgrade \
 && apt-get -y install git golang

RUN cd \
 && git clone https://github.com/jsha/minica.git \
 && cd minica \
 && go build \
 && cp minica /usr/bin

CMD sleep infinity
