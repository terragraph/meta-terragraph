addhandler sync_yocto_check_eventhandler
sync_yocto_check_eventhandler[eventmask] = "bb.event.BuildStarted"

sync_yocto_validate () {
    set +x
    # Do we know where we are?
    [ -n "${META_TERRAGRAPH_DIR}" ] || return 0

    ABS_OEROOT=$(realpath ${META_TERRAGRAPH_DIR})
    REVPOKY=$(cd "${ABS_OEROOT}/yocto/poky" && git rev-parse HEAD)
    REVMOE=$(cd "${ABS_OEROOT}/yocto/meta-openembedded" && git rev-parse HEAD)
    . ${ABS_OEROOT}/utils/sync_yocto_utils.sh
    if [ "${POKY_COMMIT}" != "${REVPOKY}" ] || [ "${OE_COMMIT}" != "${REVMOE}" ]; then
        bbwarn "yocto/poky or yocto/meta-openembedded are out of date." \
               "Use \`sync_yocto.sh.\` to resync them." \
               "A clean build is also recommended."
    fi
}

python sync_yocto_check_eventhandler ()  {
    bb.build.exec_func("sync_yocto_validate", d)
}
