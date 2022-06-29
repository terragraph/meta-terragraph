SUMMARY = "Base Utils for Terragraph images"

inherit packagegroup

debug_pkgs = "\
    devmem2 \
    evtest \
    gdb \
    python3-debugger \
    strace \
    stressapptest \
    stress-ng \
    "

fs_pkgs = "\
    e2fsprogs \
    flash-uboot \
    lsof \
    mtd-utils \
    nand-flash \
    procps \
    sysfsutils \
    util-linux \
    "

misc_pkgs = "\
    bash \
    bc \
    ckermit \
    coreutils \
    cronie \
    file \
    findutils \
    grep \
    i2c-tools \
    ldd \
    less \
    lrzsz \
    memtester \
    mobd \
    netcat-openbsd \
    openssh-sftp-server \
    pciutils \
    rsync \
    rsyslog \
    runit \
    screen \
    tzdata \
    usbutils \
    sysstat \
    uucp \
    vim-tiny \
    watchdog \
    wifitest \
    tg-netns \
    utf8 \
    gpsd \
    webui \
    esp-slip \
    "

net_pkgs = "\
    chrony \
    chronyc \
    dhcp-server \
    dhcp-client \
    ethtool \
    iftop \
    iperf3 \
    iproute2 \
    iproute2-tc \
    iproute2-lnstat \
    iproute2-ifstat \
    iproute2-genl \
    iproute2-nstat \
    iproute2-ss \
    iptables \
    iputils \
    liboping \
    mtr \
    net-snmp-server-snmpd \
    net-tools \
    resolvconf \
    sntp \
    tcpdump \
    traceroute \
    radvd \
    iw \
    hostapd \
    wpa-supplicant \
    rng-tools \
    "

RRECOMMENDS_${PN} = "\
    ${debug_pkgs} \
    ${fs_pkgs} \
    ${misc_pkgs} \
    ${net_pkgs} \
    "
