# NOTE:
# When using MAVEN_OPTS for proxy configuration, build failures have been
# observed depending on the Maven and JDK versions used in the build image.
#
# Observations:
# - Maven 3.9.5 + OpenJDK 21: build failed in proxy environments.
# - Maven 3.9.16 + OpenJDK 21: build failed in proxy environments.
# - OpenJDK 17 may fail even with Maven versions earlier than 3.9.
# - Maven 3.8.7 + OpenJDK 21 has shown stable behavior.
#
# Proxy configuration is now provided via Maven settings.xml instead of
# MAVEN_OPTS.
FROM maven:3.9.16-eclipse-temurin-21 AS build
WORKDIR /build
COPY jwt-server/pom.xml /build
COPY jwt-server/src /build/src
COPY docker/dev/common/oauth2/tomcat/application.properties /build/src/main/resources

ARG GFDOCKER_PROXY_HOST
ARG GFDOCKER_PROXY_PORT
ARG GFDOCKER_ENABLE_PROXY
ARG no_proxy

ENV GFDOCKER_PROXY_HOST=${GFDOCKER_PROXY_HOST}
ENV GFDOCKER_PROXY_PORT=${GFDOCKER_PROXY_PORT}
ENV no_proxy=${no_proxy}

RUN mkdir -p /root/.m2

COPY docker/dev/common/oauth2/tomcat/settings.xml.in \
    /root/.m2/settings.xml.in

# Environment variable references such as ${env.GFDOCKER_PROXY_PORT}
# cannot be reliably used for proxy.port with Maven 3.9.16, therefore
# actual values are substituted into a template during image build.
RUN if [ "$GFDOCKER_ENABLE_PROXY" = "true" ]; then \
    sed \
        -e "s|@PROXY_HOST@|$GFDOCKER_PROXY_HOST|g" \
        -e "s|@PROXY_PORT@|$GFDOCKER_PROXY_PORT|g" \
        -e "s|@NO_PROXY@|$no_proxy|g" \
        /root/.m2/settings.xml.in \
        > /root/.m2/settings.xml; \
    fi

RUN mvn package

FROM tomcat:10.1.30-jdk21
ADD docker/dev/common/oauth2/tomcat/conf/server.xml /usr/local/tomcat/conf/server.xml
COPY --from=0 /build/target/jwt-server.war /usr/local/tomcat/

ADD docker/dev/common/oauth2/tomcat/setup.sh /setup.sh

CMD ["catalina.sh", "run"]
