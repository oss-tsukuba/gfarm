# Gfarm/BB - Gfarm file system for node-local burst buffer

## About Gfarm/BB

Gfarm/BB is a temporal on-demand file system for a burst buffer
node-local storages to exploit node-local storages.

## How to use
```
gfarmbb -h hostfile -scr /scr/scratch -m /scr/gfarmbb start
...
gfarmbb -h hostfile -scr /scr/scratch -m /scr/gfarmbb stop
```

## Usage
```
usage: gfarmbb [-h hostfile] [-scr scratch_dir] [-m mount_point] [options] start | stop

options:
        -e
            excludes Gfmd node from file system nodes.
        -h hostfile
            specifies a hostfile.
        -scr scratch_dir
            specifies a scratch directory. (/tmp/gfarmbb-scratch)
        -l
            enables Gfarm/BB access from login nodes.
        -L log_dir
            specifies a log directory. (/tmp/gfarmbb-scratch/log)
        -m mount_point
            specifies a mount point.
        -p period
            specifies a period for a Gfarm shared key in second.
            Default is 86400 seconds (1 day).
        -c
            generates C-shell commands on stdout.
        -s
            generates Bourne shell commands on stdout.
            This is default.
```

## Environment variables
```
PRUN_RSH
	remote shell alterntive
PRUN_RCP
	remote copy alternative.  When it is no, use remote shell.
GFARMBB_START_HOOK
	hook program executed on all hosts before creating Gfarm/BB
GFARMBB_STOP_HOOK
	hook program executed on all hosts after terminating Gfarm/BB
```
## Recommended Linux kernel parameter change

The number of file descriptors and listen backlogs are set to 262144
and 2048, respectively, by the gfarmbb script.  To reflect the setting,
the kernel parameter may need to be changed.  On the other hand, if
you need more, please change the gfarmbb script.

### the number of file descriptors

262144 or greater is recommended.

* /etc/security/limits.conf
```
* - nofile 262144
```

If fs.file-max is less than the above, increase it.
```
$ cat /proc/sys/fs/file-max
3240661
```
In this case, there is no need to increase, however, if it is not your
case, increase it.
* /etc/sysctl.conf
```
fs.file-max = 262144
```

### the number of listen backlogs

syn_backlog is recommended to be larger than the number of client
processes.  listen backlog is also better to be increased.

* /etc/sysctl.conf
```
net.core.somaxconn = 2048
net.ipv4.tcp_max_syn_backlog = 262144
```

The current value can be shown as follows;
```
$ cat /proc/sys/net/core/somaxconn
128
$ cat /proc/sys/net/ipv4/tcp_max_syn_backlog
1024
```
