#!/bin/sh
set -xeu
/jwt/setup.sh

cp /jwt/cnf/master2.cnf /etc/my.cnf.d/
systemctl restart mysql

mysql < /jwt/sql/init_master2.sql
