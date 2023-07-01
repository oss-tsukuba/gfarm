#!/bin/sh

gfarm-prun -v sudo egrep \'\(warn\|err\)\' /var/log/syslog
