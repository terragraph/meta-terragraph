MACHINE_LIST = "ls1088 ls1012"

# Removes -dirty from version
PATCHTOOL = "git"

do_compile() {
    export ARMV8_TOOLS_DIR="${STAGING_BINDIR_TOOLCHAIN}"
    export ARMV8_TOOLS_PREFIX="${TARGET_PREFIX}"
    export CROSS_COMPILE="${WRAP_TARGET_PREFIX}"
    cd ${S}/ppa
    ./build rdb-fit ${MACHINE_LIST}
    cd ${S}
    for j in ${MACHINE_LIST} ; do
    	test -f ${S}/ppa/soc-${j}/build/obj/ppa.itb
    done
}

do_install() {
    install -d ${D}/boot/
    for j in ${MACHINE_LIST} ; do
    	install ${S}/ppa/soc-${j}/build/obj/ppa.itb ${D}/boot/ppa-${j}.itb
    done
}

do_deploy(){
    install -d ${DEPLOYDIR}
    for j in ${MACHINE_LIST} ; do
    	install ${S}/ppa/soc-${j}/build/obj/ppa.itb ${DEPLOYDIR}/ppa-${j}.itb
    done
}

