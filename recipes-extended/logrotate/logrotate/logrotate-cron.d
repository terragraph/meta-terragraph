SHELL=/bin/sh
PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin

#  m  h  dom mon dow user      command
*/15  *     * * *    root      mkdir -p /data/log; touch /data/log/logs.tar.gz; /bin/nice -n19 /usr/bin/ionice -c3 -t /usr/sbin/logrotate /etc/logrotate.conf
