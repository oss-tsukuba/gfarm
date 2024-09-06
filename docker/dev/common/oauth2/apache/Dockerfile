FROM quay.io/centos/centos:stream8

# CentOS Stream 8 reaches end-of-life at the end of May 2024
RUN for i in /etc/yum.repos.d/CentOS-*.repo; do \
	cp -p ${i} ${i}.bak && \
	sed -e '/^mirrorlist=/s/^/#/' \
	    -e '/^#baseurl=/s|^#baseurl=http://mirror|baseurl=http://vault|' \
		${i}.bak >${i}; \
    done

RUN yum -y update && yum clean all
RUN yum -y install httpd mod_ssl

COPY conf/httpd.conf /etc/httpd/conf/httpd.conf
COPY conf/ssl.conf /etc/httpd/conf.d/ssl.conf

ADD setup.sh /setup.sh
