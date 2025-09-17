FROM ubuntu

ARG DEBIAN_FRONTEND=noninteractive
ARG TZ=Asia/Tokyo
ENV TZ=${TZ}

RUN apt-get update \
 && apt-get -y upgrade \
 && apt-get -y install git golang

RUN cd \
 && git clone https://github.com/jsha/minica.git -b v1.1.0 \
 && cd minica \
 && go build \
 && cp minica /usr/bin

ENTRYPOINT ["minica"]
