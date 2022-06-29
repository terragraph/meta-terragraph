FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
SRC_URI += "file://0015-TG-Silence-scan-error-log.patch"

PACKAGECONFIG += "openssl gnutls"

