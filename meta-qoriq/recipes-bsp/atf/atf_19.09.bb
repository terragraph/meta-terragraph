DESCRIPTION = "ARM Trusted Firmware"

LICENSE = "BSD"
LIC_FILES_CHKSUM = "file://license.rst;md5=e927e02bca647e14efd87e9e914b2443"

inherit deploy

DEPENDS += "rcw u-boot"
DEPENDS_append_lx2160a += "ddr-phy"
do_compile[depends] += "rcw:do_deploy u-boot:do_deploy"

S = "${WORKDIR}/git"

SRC_URI = "git://source.codeaurora.org/external/qoriq/qoriq-components/atf;nobranch=1 \
           file://0001-Makefile-add-CC-gcc.patch \
           file://0001-Adaptations-for-Puma.patch \
           "
# tag LSDK-19.09-update-311219
SRCREV = "bf83b558cd535ae0272b5e5985d1c7d269b5b020"

COMPATIBLE_MACHINE = "(qoriq)"
PACKAGE_ARCH = "${MACHINE_ARCH}"
PLATFORM = "${MACHINE}"
PLATFORM_ls1088ardb-pb = "ls1088ardb"
# requires CROSS_COMPILE set by hand as there is no configure script
export CROSS_COMPILE="${TARGET_PREFIX}"
export ARCH="arm64"
# Let the Makefile handle setting up the CFLAGS and LDFLAGS as it is a standalone application
CFLAGS[unexport] = "1"
LDFLAGS[unexport] = "1"
AS[unexport] = "1"
LD[unexport] = "1"

do_configure[noexec] = "1"

do_unpack_extra() {
  cd ${S}
  cp -a plat/nxp/soc-ls1088/ls1088ardb plat/nxp/soc-ls1088/ls1088apuma
  sed -i 's/ardb/apuma/g' plat/nxp/soc-ls1088/ls1088apuma/platform.mk
  git add -f plat/nxp/soc-ls1088/ls1088apuma
  git -c user.name="${PATCH_GIT_USER_NAME}" -c user.email="${PATCH_GIT_USER_EMAIL}" commit -a -m "Initial copy of base board sources"
}

do_compile() {
  mkdir -p ${S}/out

  # For reproducible builds
  EXTRA_ARGS="BUILD_STRING=fb BUILD_MESSAGE_TIMESTAMP=\"(reproducible)\""

  # Build for fb-ls1048a-puma
  # Build BL2
  oe_runmake PLAT=ls1088apuma bl2 BOOT_MODE=qspi pbl RCW=${DEPLOY_DIR_IMAGE}/rcw/fb-ls1048a-puma/FS_PPP_0x15_0x14/rcw_1600_qspi.bin ${EXTRA_ARGS}
  # Build BL3 (BL31 + OPTEE + U-Boot)
  oe_runmake PLAT=ls1088apuma fip BL33=${DEPLOY_DIR_IMAGE}/u-boot.bin-fb-ls1048a-puma ${EXTRA_ARGS}
  cp ${S}/build/ls1088apuma/release/bl2_qspi.pbl ${S}/out/bl2_qspi_fb-ls1048a-puma.pbl
  cp ${S}/build/ls1088apuma/release/fip.bin ${S}/out/fip_fb-ls1048a-puma.bin
  oe_runmake PLAT=ls1088apuma clean

  # Build for fb-ls1048a-puma2
  # Build BL2
  oe_runmake PLAT=ls1088apuma bl2 BOOT_MODE=qspi pbl RCW=${DEPLOY_DIR_IMAGE}/rcw/fb-ls1048a-puma/FSNNNNQQQQ_PPP_0x19_0x14/rcw_1600_qspi.bin ${EXTRA_ARGS}
  # Build BL3 (BL31 + OPTEE + U-Boot)
  oe_runmake PLAT=ls1088apuma fip BL33=${DEPLOY_DIR_IMAGE}/u-boot.bin-fb-ls1048a-puma2 ${EXTRA_ARGS}
  cp ${S}/build/ls1088apuma/release/bl2_qspi.pbl ${S}/out/bl2_qspi_fb-ls1048a-puma2.pbl
  cp ${S}/build/ls1088apuma/release/fip.bin ${S}/out/fip_fb-ls1048a-puma2.bin

  # Build for fb-ls1048a-mbh
  # Build BL2
  oe_runmake PLAT=ls1088apuma bl2 BOOT_MODE=qspi pbl RCW=${DEPLOY_DIR_IMAGE}/rcw/fb-ls1048a-mbh/FS_PPP_0x15_0x13/rcw_1600_qspi.bin ${EXTRA_ARGS}
  # Build BL3 (BL31 + OPTEE + U-Boot)
  oe_runmake PLAT=ls1088apuma fip BL33=${DEPLOY_DIR_IMAGE}/u-boot.bin-fb-ls1048a-mbh ${EXTRA_ARGS}
  cp ${S}/build/ls1088apuma/release/bl2_qspi.pbl ${S}/out/bl2_qspi_fb-ls1048a-mbh.pbl
  cp ${S}/build/ls1088apuma/release/fip.bin ${S}/out/fip_fb-ls1048a-mbh.bin
}

do_install() {
  install -d ${D}/boot/atf
  cp ${S}/out/bl2_qspi_fb-ls1048a-puma.pbl ${D}/boot/atf/
  cp ${S}/out/fip_fb-ls1048a-puma.bin ${D}/boot/atf/
  cp ${S}/out/bl2_qspi_fb-ls1048a-puma2.pbl ${D}/boot/atf/
  cp ${S}/out/fip_fb-ls1048a-puma2.bin ${D}/boot/atf/
  cp ${S}/out/bl2_qspi_fb-ls1048a-mbh.pbl ${D}/boot/atf/
  cp ${S}/out/fip_fb-ls1048a-mbh.bin ${D}/boot/atf/
}

do_deploy() {
  install -d ${DEPLOYDIR}/atf
  cp ${D}/boot/atf/bl2_qspi_fb-ls1048a-puma.pbl ${DEPLOYDIR}/atf/
  cp ${D}/boot/atf/fip_fb-ls1048a-puma.bin ${DEPLOYDIR}/atf/
  cp ${D}/boot/atf/bl2_qspi_fb-ls1048a-puma2.pbl ${DEPLOYDIR}/atf/
  cp ${D}/boot/atf/fip_fb-ls1048a-puma2.bin ${DEPLOYDIR}/atf/
  cp ${D}/boot/atf/bl2_qspi_fb-ls1048a-mbh.pbl ${DEPLOYDIR}/atf/
  cp ${D}/boot/atf/fip_fb-ls1048a-mbh.bin ${DEPLOYDIR}/atf/
}

addtask unpack_extra after do_unpack before do_patch
addtask deploy after do_install

FILES_${PN} += "/boot"
BBCLASSEXTEND = "native nativesdk"
