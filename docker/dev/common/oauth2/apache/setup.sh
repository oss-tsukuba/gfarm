#!/bin/bash

cp /mnt/jwt-server/jwt-servercert.pem /etc/httpd/conf/httpdcert.pem
cp /mnt/jwt-server/jwt-serverkey.pem /etc/httpd/conf/httpdkey.pem
/usr/sbin/httpd
