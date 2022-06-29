# Build without sensord to save space.
PACKAGECONFIG_remove = "sensord"

# Mapping sensor label by config
FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
SRC_URI += " \
    file://sensors_label.conf \
"

do_install_append() {
    install -d ${D}${sysconfdir}/sensors.d
    install -m 0644 sensors_label.conf ${D}${sysconfdir}/sensors.d/sensors.conf
}
