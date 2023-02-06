#!/usr/bin/bash

echo "changeit" | keytool  -noprompt -import -alias gfarm \
  -keystore /opt/java/openjdk/lib/security/cacerts \
  -file /mnt/tomcat/cacert.pem

cp /usr/local/tomcat/jwt-server.war /usr/local/tomcat/webapps
