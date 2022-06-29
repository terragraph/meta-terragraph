SUMMARY = "E2E node packages for Terragraph images"

inherit packagegroup

prod_pkgs = "\
    e2e-minion \
    fluent-bit \
    stats \
    stats-snmp \
    led-agent \
    "

debug_pkgs = "\
    openssl-bin \
    "

test_pkgs = "\
    e2e-controller \
    r2d2 \
    tg \
    "

RDEPENDS_${PN} = " \
    ${prod_pkgs} \
    ${debug_pkgs} \
    ${test_pkgs} \
"
