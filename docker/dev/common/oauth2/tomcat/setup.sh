#!/usr/bin/bash

echo "changeit" | keytool  -noprompt -import -alias gfarm \
  -keystore /opt/java/openjdk/lib/security/cacerts \
  -file /mnt/jwt-tomcat/cacert.pem

rm -rf /usr/local/tomcat/webapps/ROOT
cp /usr/local/tomcat/jwt-server.war /usr/local/tomcat/webapps/ROOT.war
