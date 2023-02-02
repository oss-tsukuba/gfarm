FROM quay.io/centos/centos:stream8

RUN yum -y update && yum clean all
RUN yum -y install httpd mod_ssl

COPY conf/httpd.conf /etc/httpd/conf/httpd.conf
COPY conf/ssl.conf /etc/httpd/conf.d/ssl.conf

ADD setup.sh /setup.sh
