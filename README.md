STATSD-C
========

This is the code from jbuchbinder/statsd-c with the JSON stuff and Gaglia/Gmetric stuff
removed. I want a minimal statsd server written in C for Graphite. I don't care for Ganglia
or JSON, so I have no use for that code.

I also plan to build this using libevent asynchronous IO instead of pthreads, so the threading
is going away soon.

BUILD
-----

Type "make". press enter. It compiles on Mac OS X.


USAGE
-----

    Usage: statsd [-hDdfFc] [-p port] [-m port] [-s file] [-G host] [-g port] [-S spoofhost] [-P prefix] [-l lockfile] [-T percentiles]
        -p port           set statsd udp listener port (default 8125)
        -m port           set statsd management port (default 8126)
        -s file           serialize state to and from file (default disabled)
        -G host           ganglia host (default disabled)
        -g port           ganglia port (default 8649)
        -S spoofhost      ganglia spoof host (default statsd:statsd)
        -P prefix         ganglia metric prefix (default is none)
        -l lockfile       lock file (only used when daemonizing)
        -h                this help display
        -d                enable debug
        -D                daemonize
        -f                enable friendly mode (breaks wire compatibility)
        -F seconds        set flush interval in seconds (default 10)
        -c                clear stats on startup
        -T                percentile thresholds, csv (defaults to 90)
