# NOTE:
# In proxy environments, build failures have been observed depending on
# the Maven and JDK versions used in the build image.

# Observations:
# - Maven 3.9.x may fail to build.
# - OpenJDK 17 may fail even with Maven versions earlier than 3.9.
# - Maven 3.8.7 with OpenJDK 21 has shown stable behavior.
FROM jelastic/maven:3.8.7-openjdk-21.ea-b7 AS build
WORKDIR /build
COPY jwt-server/pom.xml /build
COPY jwt-server/src /build/src
COPY docker/dev/common/oauth2/tomcat/application.properties /build/src/main/resources
ARG MAVEN_OPTS
RUN mvn package

FROM tomcat:10.1.30-jdk21
ADD docker/dev/common/oauth2/tomcat/conf/server.xml /usr/local/tomcat/conf/server.xml
COPY --from=0 /build/target/jwt-server.war /usr/local/tomcat/

ADD docker/dev/common/oauth2/tomcat/setup.sh /setup.sh

CMD ["catalina.sh", "run"]
