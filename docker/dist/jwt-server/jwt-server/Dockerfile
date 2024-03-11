FROM rockylinux/rockylinux:9

RUN dnf -y update \
 && dnf -y install \
    git java-devel tomcat httpd mod_ssl maven mariadb-server

COPY jwt-server.conf /etc/httpd/conf.d/
COPY server.xml /etc/tomcat/

RUN git clone https://github.com/oss-tsukuba/jwt-server.git

COPY application.properties /jwt-server/src/main/resources/

RUN cd jwt-server \
 && mvn package \
 && cp target/jwt-server.war /usr/share/tomcat/webapps/ROOT.war

COPY my.cnf.d/ /
COPY init.sql /jwt-server/ddl/
COPY init_master.sql /jwt-server/ddl/
COPY init_slave.sql /jwt-server/ddl/
COPY setup.sh /
COPY setup_slave.sh /

RUN systemctl enable httpd \
 && systemctl enable tomcat \
 && systemctl enable mariadb

CMD ["/sbin/init"]
