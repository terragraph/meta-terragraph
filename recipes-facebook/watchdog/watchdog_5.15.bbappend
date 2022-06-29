# Terragraph-specific watchdog tweaks

FILESEXTRAPATHS_prepend := "${THISDIR}/watchdog:"

SRC_URI += "file://link_monit.sh \
            file://monit_config.sh \
            file://monit_init.sh \
            file://monit_utils.sh \
            file://monit_watch.sh \
            file://pop_reachable_monit.sh \
            file://progress_monit.sh \
            file://watchdog.sh \
           "

# Platforms can add extra scripts to this list
WATCHDOG_SCRIPTS = "link_monit.sh pop_reachable_monit.sh progress_monit.sh"

# Yocto has changed from watchdog.sh to watchdog. Change it back.
INITSCRIPT_NAME_${PN} = "watchdog.sh"

do_install_append() {
    # Overwrite watchdog startup with custom script
    # The base recipe will update-rc.d this for us.
    install -m 755 ${WORKDIR}/watchdog.sh ${D}${sysconfdir}/init.d/watchdog.sh

    # Yocto changed from watchdog.sh to watchdog.  Remove the default one.
    rm -f ${D}${sysconfdir}/init.d/watchdog

    # Watchdog configuration and helpers
    install -m 755 ${WORKDIR}/monit_config.sh ${D}${sysconfdir}/monit_config.sh
    install -m 755 ${WORKDIR}/monit_init.sh ${D}${sysconfdir}/monit_init.sh
    install -m 755 ${WORKDIR}/monit_watch.sh ${D}/usr/sbin/monit_watch
    install -m 755 ${WORKDIR}/monit_utils.sh ${D}${sysconfdir}/monit_utils.sh

    # Watchdog scripts
    install -d ${D}${sysconfdir}/watchdog.d
    for f in ${WATCHDOG_SCRIPTS}; do
        install -m 755 ${WORKDIR}/$f ${D}${sysconfdir}/watchdog.d
    done
}

RDEPENDS_${PN} += "bash"
