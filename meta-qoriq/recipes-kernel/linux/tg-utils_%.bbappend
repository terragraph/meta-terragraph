FILESEXTRAPATHS_prepend := "${THISDIR}/tg-utils:"

SRC_URI += "file://kernel_dump \
            file://process_dump \
           "
# Add to sysdump scripts. Since we want to install under system subdirectory
# can't use SYSDUMP_SCRIPTS.  Need to  extend install to add them to right
# location
do_install_append () {
        install -d ${D}/etc/sysdump/system
        install -m 755  ${WORKDIR}/kernel_dump ${D}/etc/sysdump/system/kernel_dump
        install -m 755  ${WORKDIR}/process_dump ${D}/etc/sysdump/system/process_dump
}


