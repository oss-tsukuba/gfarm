FROM maven:3.6.3-jdk-11-slim as BUILD
WORKDIR /build
COPY jwt-server/pom.xml /build
COPY jwt-server/src /build/src
COPY docker/dev/common/oauth2/tomcat/application.properties /build/src/main/resources
ARG MAVEN_OPTS
RUN mvn package

FROM tomcat:9.0.71-jdk11
ADD docker/dev/common/oauth2/tomcat/conf/server.xml /usr/local/tomcat/conf/server.xml
COPY --from=0 /build/target/jwt-server.war /usr/local/tomcat/

ADD docker/dev/common/oauth2/tomcat/setup.sh /setup.sh

CMD ["catalina.sh", "run"]
