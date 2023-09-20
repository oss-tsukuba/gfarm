#!/bin/sh
set -xeu

update-ca-trust
keytool -noprompt -import -cacerts -storepass changeit -alias minica \
       -file /usr/share/pki/ca-trust-source/anchors/minica.crt

systemctl restart tomcat

while [ ! -e /var/lib/mysql/mysql.sock ]; do
	sleep 1
done
cd jwt-server/ddl
mysql < init.sql
mysql -u gfarm -pgfarm123 gfarmdb < jwt-server.ddl

cp /mysql_slave.cnf /etc/my.cnf.d/
systemctl restart mysql

mysql < init_slave.sql
