#!/bin/sh

gfarm-prun -v -a sudo egrep \'\(warn\|err\)\' /var/log/syslog
