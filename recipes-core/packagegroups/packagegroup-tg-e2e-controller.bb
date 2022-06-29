SUMMARY = "E2E controller packages for Terragraph images"

inherit packagegroup

RDEPENDS_${PN} = "\
    api \
    e2e-controller \
    openssh \
    opentracker \
    stats-aggregator \
    tg \
"
