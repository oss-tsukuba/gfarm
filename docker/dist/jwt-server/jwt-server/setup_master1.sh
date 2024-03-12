#!/bin/sh
set -xeu
/jwt/setup.sh

cp /jwt/cnf/master1.cnf /etc/my.cnf.d/
systemctl restart mysql

mysql < /jwt/sql/init_master1.sql
