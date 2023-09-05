FROM almalinux:8

RUN dnf -y update \
 && dnf -y install \
    git java-11-devel tomcat httpd mod_ssl maven mariadb-server \
 && alternatives --set java java-11-openjdk.x86_64

COPY jwt-server.conf /etc/httpd/conf.d/
COPY server.xml /etc/tomcat/

RUN git clone https://github.com/oss-tsukuba/jwt-server.git

COPY application.properties /jwt-server/src/main/resources/

RUN cd jwt-server \
 && mvn package \
 && cp target/jwt-server.war /usr/share/tomcat/webapps/ROOT.war

COPY init.sql /jwt-server/ddl/
COPY setup.sh /

RUN systemctl enable httpd \
 && systemctl enable tomcat \
 && systemctl enable mariadb

CMD ["/sbin/init"]
