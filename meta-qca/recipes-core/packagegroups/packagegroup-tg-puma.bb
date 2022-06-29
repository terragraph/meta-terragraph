SUMMARY = "Routing and firmware for the Puma/QCA platform."

inherit packagegroup

uboot_pkgs = " \
    e2fsprogs-resize2fs \
    esp-fw \
    gptfdisk \
    tg-utils \
    u-boot-fw-utils \
    u-boot-qoriq \
"

routing_pkgs = " \
    coop \
    exabgp-to-openr \
    frrouting-bgpd \
    gpp-aioptool \
    jool \
    jool-kernel-module \
    openr-fib-vpp \
    openr-puff \
    ppfe-firmware \
    tg-vpp \
    vpp \
    vpp-chaperone \
    vpp-esmc \
    vpp-ptptc \
    vpp-tgcfg \
"

wireless_pkgs = " \
    kernel-module-rte-kni \
    kernel-module-wil6210 \
    kernel-module-terragraph-qca \
    kernel-module-dpdk-dhd \
    kernel-module-zl3079x \
    terragraph-hwsim \
    pktgen-dpdk \
    wigig-dpdk-examples \
    wigig-fw-bin \
    wigig-tg-scripts \
    wigig-utils \
    wigig-utils-oss \
"

misc_pkgs = " \
    vsc8254 \
"

RDEPENDS_${PN} = " \
    ${uboot_pkgs} \
    ${routing_pkgs} \
    ${wireless_pkgs} \
    ${misc_pkgs} \
"
