FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"

SRC_URI += "file://busybox.cfg \
            file://syslog.conf \
           "

# Force use of real sed
RDEPENDS_${PN} += "sed"
