SUMMARY = "Debug tools for wil6210"
DESCRIPTION = "Debug utilities for Wilocity 60g WiFi card wil6210"
SECTION = "net"
LICENSE = "CLOSED"

WIGIG_UTILS_DIR ?= "qca6430-tg-1-0_qca_oem_3pp.git/apss_proc/out/proprietary/qca-wigig-utils-0.1/qca/src/wigig-utils"

S = "${WORKDIR}/wigig-utils/debug-tools"

inherit pkgconfig

DEPENDS_class-target += "libnl"

# QCOM distfiles use rather deep nesting, make it shorter for convenience
do_src_move() {
	rm -rf ${WORKDIR}/wigig-utils
	mv ${WORKDIR}/${WIGIG_UTILS_DIR} ${WORKDIR}
}

do_unpack_append () {
    bb.build.exec_func('do_src_move', d)
}

# Makefiles by QCOM contain multiple bugs, not honoring dependencies between
# libraries and binaries linking to these libraries is most common. Another
# is hardcoding paths in plain Makefile and inclusion of 'install' into the
# 'all' target in Makefile.3pp. We use plain version to build binaries and
# .3pp version to install them, since neither is sutable to be used for both
# purposes.
PARALLEL_MAKE = ""

# Override variables to point build to target toolchain and staging dir
EXTRA_OEMAKE += 'WIGIG_3PP_BUILD=TRUE \
    CXX="${CXX} ${CXXFLAGS} ${LDFLAGS}" \
    CC="${CC} ${CFLAGS} ${LDFLAGS}" \
    STAGING_DIR_PLATFORM=${STAGING_DIR_TARGET} \
    SYSROOT_CPP=${STAGING_DIR_TARGET} \
'
EXTRA_OEMAKE_append_task-install = 'INSTALL_ROOT=${D} \
    INSTALL_DIR="install -d -m 0755" \
    INSTALL_BIN="install -m 0755" \
    -f Makefile.3pp \
'

# Only build and install wiburn tool for native
do_compile_class-native () {
    oe_runmake make_wiburn
}

do_install_class-native () {
	install -d ${D}${bindir} ${D}${libdir}
	install ${S}/lib/FlashAcss/libwigig_flashaccess.so ${D}${libdir}
	install ${S}/lib/utils/libwigig_utils.so ${D}${libdir}
	install ${S}/lib/WlctPciAcss/libwigig_pciaccess.so ${D}${libdir}
	install ${S}/wiburn/wigig_wiburn ${D}${bindir}
}

do_install () {
    oe_runmake install
}

FILES_${PN} += "${libdir}/libwigig_*.so"
FILES_SOLIBSDEV = ""

BBCLASSEXTEND = "native nativesdk"
