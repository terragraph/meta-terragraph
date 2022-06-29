SUMMARY = "Broadcom wireless firmware for Terragraph images"

inherit packagegroup

RDEPENDS_${PN} = "\
    brcm20130-fw \
    brcm20130-fw-if2if \
    brcm20130-pkteng \
    broadcom-dhd \
    broadcom-scripts \
    mkfwcore \
    tps23861 \
"
