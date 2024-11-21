FROM rockylinux/rockylinux:9

RUN dnf -y update \
 && dnf -y install \
    git java-21-openjdk-devel httpd mod_ssl maven mariadb-server wget

RUN alternatives --set java java-21-openjdk.x86_64
RUN alternatives --set javac java-21-openjdk.x86_64

ARG TOMCAT_VER=10.1.33

RUN wget https://archive.apache.org/dist/tomcat/tomcat-10/v$TOMCAT_VER/bin/apache-tomcat-$TOMCAT_VER.tar.gz
RUN tar -xvf apache-tomcat-$TOMCAT_VER.tar.gz -C /opt
RUN ln -s /opt/apache-tomcat-$TOMCAT_VER /opt/tomcat

RUN useradd -s /sbin/nologin tomcat
RUN chown -R tomcat: /opt/apache-tomcat-$TOMCAT_VER
RUN sh -c 'chmod +x /opt/tomcat/bin/*.sh'

COPY tomcat.service /etc/systemd/system

COPY jwt-server.conf /etc/httpd/conf.d/
COPY server.xml /opt/tomcat/conf/
RUN rm -rf /opt/tomcat/webapps/*

RUN git clone https://github.com/oss-tsukuba/jwt-server.git

COPY application.properties /jwt-server/src/main/resources/

ENV JAVA_HOME="/usr/lib/jvm/java"

RUN cd jwt-server \
 && mvn package \
 && cp target/jwt-server.war /opt/tomcat/webapps/ROOT.war

COPY cnf/charset.cnf /etc/my.cnf.d/

RUN systemctl enable httpd \
 && systemctl enable tomcat \
 && systemctl enable mariadb

CMD ["/sbin/init"]
