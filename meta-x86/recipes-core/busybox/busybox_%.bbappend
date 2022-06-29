FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"

SRC_URI += "file://syslog.conf \
           "

# Force use of real sed
RDEPENDS_${PN} += "sed"
