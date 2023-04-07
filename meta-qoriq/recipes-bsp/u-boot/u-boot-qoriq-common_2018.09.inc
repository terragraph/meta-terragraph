inherit fsl-u-boot-localversion

LICENSE = "GPLv2 & BSD-3-Clause & BSD-2-Clause & LGPL-2.0 & LGPL-2.1"
LIC_FILES_CHKSUM = " \
    file://Licenses/gpl-2.0.txt;md5=b234ee4d69f5fce4486a80fdaf4a4263 \
    file://Licenses/bsd-2-clause.txt;md5=6a31f076f5773aabd8ff86191ad6fdd5 \
    file://Licenses/bsd-3-clause.txt;md5=4a1190eac56a9db675d58ebe86eaf50c \
    file://Licenses/lgpl-2.0.txt;md5=5f30f0716dfdd0d91eb439ebec522ec2 \
    file://Licenses/lgpl-2.1.txt;md5=4fbd65380cdd255951079008b364516c \
"

SRC_URI = "git://github.com/nxp-qoriq/u-boot.git;protocol=https;nobranch=1"
SRCREV= "80b2d2bc4cab0a8363c9b7eba8064b1795f12670"

S = "${WORKDIR}/git"
LOCALVERSION = "+fsl"
# For reproducible builds
SCMVERSION = "n"

# For Terragraph patches
SRC_URI_append += "\
    file://0001-Add-terragraph-config.patch \
    file://0002-Add-testcode-support.patch \
    file://0003-Add-gpio-support.patch \
    file://0004-Add-rcw-crc.patch \
    file://0005-Allow-GPT-to-fit-into-first-4K-of-disk.patch \
    file://0006-Force-pci_init-in-uboot.patch \
    file://0007-Relax-check-for-valid-environment-in-__hwconfig.patch \
    file://0008-u-boot-Add-squashfs-support.patch \
    file://0009-squashfs-Add-symlink-support.patch \
    file://0010-Bugfix-Don-t-overwrite-bootcmd-env-var-with-garbage.patch \
    file://0011-Add-zstd-to-u-boot-repo.patch \
    file://0012-Add-support-for-xxhash.patch \
    file://0013-Add-support-for-zstd-compression-for-squashfs.patch \
"

SRC_URI_append_ls1088a += "\
    file://0008-Add-support-for-LS1088ARBD.patch \
    file://0009-Uboot-gpio-support-for-ls1088.patch \
    file://0010-Added-support-for-ls1088-watchdog.patch \
"

SRC_URI_append_ls1048atg += "\
    file://0011-Add-ls1048atg-board.patch \
    file://0012-Disable-errate-a009007-application.patch \
    file://0013-ls1048atg-mmc-fix.patch \
    file://0014-ls1048-gpio-fixes.patch \
    file://0015-ls1048atg-ddr-2100-rev2.patch \
    file://0016-rev2-network-changes.patch \
    file://0017-Turn-on-secondary-led-if-bb-present-and-also-set-rf_.patch \
    file://0018-Clear-mac-addresses-when-env-clear-is-required.patch \
    file://0019-Read-NIC-mac-addresses-from-EEPROM-on-TG-boards.patch \
    file://0020-Append-bootargs-to-bootargs-var-in-chosen-if-any.patch \
"

SRC_URI_append_fb-ls1048a-puma += "\
    file://0021-Create-new-board-FB_LS1048A_PUMA-and-added-support-f.patch \
    file://0022-Fix-up-GPIO-for-fb-ls1048a-puma.patch \
    file://0023-Check-for-valid-phy-address-before-eth-init.patch \
    file://0026-Adaptations-for-TF-A-boot.patch \
    file://0029-Enable-loading-kernel-from-squashfs.patch \
    file://0030-Enable-zstd-in-squashfs.patch \
"

SRC_URI_append_fb-ls1048a-puma2 += "\
    file://0024-Initialize-Puma-Proto2-sources-from-Puma-Proto-1.patch \
    file://0025-Enabled-support-for-LS1048a-PUMA-Proto-2-board.patch \
    file://0027-Adaptations-for-TF-A-boot.patch \
    file://0028-Deassert-RESETn-of-88E1545-QSGMII.patch \
    file://0030-Enable-loading-kernel-from-squashfs.patch \
    file://0031-Enable-zstd-in-squashfs.patch \
"

SRC_URI_append_fb-ls1048a-mbh += "\
    file://0032-Initializing-MBH-with-Puma-Proto-2-sources.patch \
    file://0033-Add-support-for-LS1048A-MBH-board.patch \
    file://0034-VSC8254-move-to-MDIO2-and-change-mdio-address.patch \
"

LS1012A_SRCS = "\
    arch/arm/dts/fsl-ls1012a-frdm.dts \
    arch/arm/dts/fsl-ls1012a-frdm.dtsi \
    include/configs/ls1012afrdm.h \
    configs/ls1012afrdm_qspi_defconfig \
    board/freescale/ls1012afrdm \
"

LS1048A_SRCS = "\
    arch/arm/dts/fsl-ls1088a-rdb.dts \
    include/configs/ls1088ardb.h \
    configs/ls1088ardb_sdcard_qspi_defconfig  \
    board/freescale/ls1088a/Kconfig \
    board/freescale/ls1088a/Makefile \
    board/freescale/ls1088a/README \
    board/freescale/ls1088a/MAINTAINERS \
    board/freescale/ls1088a/ls1088a.c \
    board/freescale/ls1088a/eth_ls1088ardb.c \
"

FB_LS1048A_PUMA_SRCS = "\
    include/configs/ls1088ardb.h \
    board/freescale/ls1088a/Kconfig \
    board/freescale/ls1088a/Makefile \
    board/freescale/ls1088a/README \
    board/freescale/ls1088a/MAINTAINERS \
    board/freescale/ls1088a/ls1088a.c \
    board/freescale/ls1088a/eth_ls1088ardb.c \
"

do_unpack_extra() {
    cd ${S}
    # Copy LS1012AFRDM sources to LS1012ATG as the base
    for src in ${LS1012A_SRCS}; do
        dst=$(echo $src | sed 's/frdm/tg/')
        rm -rf ${dst}
        cp -r ${src} ${dst}
    done
    mv board/freescale/ls1012atg/ls1012afrdm.c board/freescale/ls1012atg/ls1012atg.c
    # Stage copied files for commit
    for src in ${LS1012A_SRCS}; do
        dst=$(echo $src | sed 's/frdm/tg/')
        git add -f ${dst}
    done

    # Copy LS1088ARDB sources to LS1048ATG as the base
    /bin/mkdir -p board/freescale/ls1048a
    for src in ${LS1048A_SRCS}; do
        dst=$(echo $src | sed 's/1088/1048/g')
        dst=$(echo $dst | sed 's/rdb/tg/')
        rm -rf ${dst}
        cp -r ${src} ${dst}
    done
    # Stage copied files for commit
    for src in ${LS1048A_SRCS}; do
        dst=$(echo $src | sed 's/1088/1048/g')
        dst=$(echo $dst | sed 's/rdb/tg/')
        git add -f ${dst}
    done

    # Copy LS1088ARDB sources to FB_LS1048A_PUMA as the base
    cp arch/arm/dts/fsl-ls1088a-rdb.dts arch/arm/dts/fb-ls1048a-puma.dts
    cp configs/ls1088ardb_qspi_defconfig configs/fb-ls1048a-puma_qspi_defconfig
    /bin/mkdir -p board/freescale/fb_ls1048a_puma
    for src in ${FB_LS1048A_PUMA_SRCS}; do
        dst=$(echo $src | sed 's/ls1088a/fb_ls1048a_puma/g')
        dst=$(echo $dst | sed 's/rdb//')
        rm -rf ${dst}
        cp -r ${src} ${dst}
    done

    # Stage copied files for commit
    git add arch/arm/dts/fb-ls1048a-puma.dts
    git add configs/fb-ls1048a-puma_qspi_defconfig
    for src in ${FB_LS1048A_PUMA_SRCS}; do
        dst=$(echo $src | sed 's/ls1088a/fb_ls1048a_puma/g')
        dst=$(echo $dst | sed 's/rdb//')
        git add -f ${dst}
    done

    # Copy LS1088ARDB sources to FB_LS1048A_PUMA2 as the base
    cp arch/arm/dts/fsl-ls1088a-rdb.dts arch/arm/dts/fb-ls1048a-puma-proto2.dts
    cp configs/ls1088ardb_qspi_defconfig configs/fb-ls1048a-puma2_qspi_defconfig
    /bin/mkdir -p board/freescale/fb_ls1048a_puma2
    for src in ${FB_LS1048A_PUMA_SRCS}; do
        dst=$(echo $src | sed 's/ls1088a/fb_ls1048a_puma2/g')
        dst=$(echo $dst | sed 's/rdb//')
        rm -rf ${dst}
        cp -r ${src} ${dst}
    done

    # Stage copied files for commit
    git add arch/arm/dts/fb-ls1048a-puma-proto2.dts
    git add configs/fb-ls1048a-puma2_qspi_defconfig
    for src in ${FB_LS1048A_PUMA_SRCS}; do
        dst=$(echo $src | sed 's/ls1088a/fb_ls1048a_puma2/g')
        dst=$(echo $dst | sed 's/rdb//')
        git add -f ${dst}
    done

    # Copy LS1088ARDB sources to FB_LS1048A_MBH as the base
    cp arch/arm/dts/fsl-ls1088a-rdb.dts arch/arm/dts/fb-ls1048a-mbh.dts
    cp configs/ls1088ardb_qspi_defconfig configs/fb-ls1048a-mbh_qspi_defconfig
    /bin/mkdir -p board/freescale/fb_ls1048a_mbh
    for src in ${FB_LS1048A_PUMA_SRCS}; do
        dst=$(echo $src | sed 's/ls1088a/fb_ls1048a_mbh/g')
        dst=$(echo $dst | sed 's/rdb//')
        rm -rf ${dst}
        cp -r ${src} ${dst}
    done

    # Stage copied files for commit
    git add arch/arm/dts/fb-ls1048a-mbh.dts
    git add configs/fb-ls1048a-mbh_qspi_defconfig
    for src in ${FB_LS1048A_PUMA_SRCS}; do
        dst=$(echo $src | sed 's/ls1088a/fb_ls1048a_mbh/g')
        dst=$(echo $dst | sed 's/rdb//')
        git add -f ${dst}
    done


    git -c user.name="${PATCH_GIT_USER_NAME}" -c user.email="${PATCH_GIT_USER_EMAIL}" commit -a -m "Initial copy of base board sources"
}
addtask unpack_extra after do_unpack before do_patch

# Use git to apply our changes
PATCHTOOL = "git"
