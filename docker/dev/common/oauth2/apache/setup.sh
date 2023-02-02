#!/bin/bash

cp /mnt/httpd/httpdcert.pem /etc/httpd/conf/httpdcert.pem
cp /mnt/httpd/httpdkey.pem /etc/httpd/conf/httpdkey.pem
/usr/sbin/httpd
