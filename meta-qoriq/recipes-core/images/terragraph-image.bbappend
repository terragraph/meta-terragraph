ROOTFS_POSTPROCESS_COMMAND_append += "build_firmware_image_fb_ls1048a_puma;"
ROOTFS_POSTPROCESS_COMMAND_append += "build_firmware_image_fb_ls1048a_puma2;"
ROOTFS_POSTPROCESS_COMMAND_append += "build_firmware_image_fb_ls1048a_mbh;"
ROOTFS_POSTPROCESS_COMMAND_append += "build_firmware_image_fb_ls1012_bobcat;"
ROOTFS_POSTPROCESS_COMMAND_append += "build_firmware_image_fb_ls1088ardb;"

build_firmware_ls1088a_mmc_image () {
	fw_name=$1
	rcw_bin=$2
	uboot_bin=$3

	# Form a fw blob for ls1088 based board which can be programmed
	# on to the mmc.  Please note the start sector is 8 so the
	# skip offsets in the dd commands below is offset by 8 compared
	# to what is printed by gdisk -l. Need to program this blob
	# starting at sector 8

# Partiion NO    Start Sector   End Sector    Size     Contents
#   5               8            2047      1020.0 KiB  rcw
#   6            2048            6143      2.0 MiB     u-boot
#   7            6144            6159      8.0 KiB     u-boot-env
#   8            8192           12287      2.0 MiB     ppa
#   9           20480           26623      3.0 MiB     dpaa2-mc
#  10           26624           28671      1024.0 KiB  dpaa2-dpl
#  11           28672           30719      1024.0 KiB  dpaa2-dpc

	fw_blob=${DEPLOY_DIR_IMAGE}/${fw_name}

	dd if=/dev/zero of=${fw_blob} count=30712 bs=512

	dd if=${DEPLOY_DIR_IMAGE}/rcw/${rcw_bin} bs=512 seek=0 of=${fw_blob}

	dd if=${DEPLOY_DIR_IMAGE}/${uboot_bin} bs=512 seek=2040  \
			conv=notrunc of=${fw_blob}

	dd if=${DEPLOY_DIR_IMAGE}/ppa-ls1088.itb bs=512 seek=8184 \
			conv=notrunc of=${fw_blob}

	dd if=${DEPLOY_DIR_IMAGE}/mc_app/mc.itb bs=512 seek=20472 \
			conv=notrunc of=${fw_blob}

	dd if=${DEPLOY_DIR_IMAGE}/mc-utils/dpl-eth.0x1D_0x0D.dtb bs=512 seek=26616 \
			conv=notrunc of=${fw_blob}

	dd if=${DEPLOY_DIR_IMAGE}/mc-utils/dpc.0x1D-0x0D.dtb bs=512 seek=28664  \
			conv=notrunc of=${fw_blob}
}

build_firmware_image_fb_ls1088ardb () {
	# Original LS1088ARDB
	build_firmware_ls1088a_mmc_image fb-ls1088ardb-sdboot-fw-blob.bin \
		ls1088ardb/FCQQQQQQQQ_PPP_H_0x1d_0x0d/rcw_1600_sd.bin \
		u-boot.bin-ls1088ardb
	# Newer LS1088ARDB-PB
	build_firmware_ls1088a_mmc_image fb-ls1088ardb-pb-sdboot-fw-blob.bin \
		ls1088ardb/FCSSRR_PPPP_0x1d_0x13/rcw_1600_sd.bin \
		u-boot.bin-ls1088ardb
}

build_firmware_image_fb_ls1048a_puma () {

	# Form a fw blob for fb-ls1048a-puma based board which can be programmed
	# on to the QSPI. This includes RCW, U-Boot binary, TF-A and DPAA
	# firmware images. The whole flash can be programmed by writing
	# resulting image at offset 0 in QSPI.
	# tg_mtdparts=4096K@0(allboot),128K@0(rcw),8K(dpl),8K(dpc),1536K(MC),8K(u-boot-env),1536K(u-boot)

	if [ ! -f ${DEPLOY_DIR_IMAGE}/atf/bl2_qspi_fb-ls1048a-puma.pbl ]; then
		return
	fi

	fw_name=fb-ls1048a-puma-qspi-fw-blob.bin
	fw_blob=${DEPLOY_DIR_IMAGE}/${fw_name}

	# Start from scratch, by filling whole image with 0xFFs
	dd if=/dev/zero bs=1K count=3224 | tr '\000' '\377' > ${fw_blob}

	# RCW
	dd if=${DEPLOY_DIR_IMAGE}/atf/bl2_qspi_fb-ls1048a-puma.pbl \
			obs=1K seek=0 conv=notrunc of=${fw_blob}

	#DPL
	dd if=${DEPLOY_DIR_IMAGE}/mc-utils/dpl-eth.0x15-0x14.dtb of=${fw_blob} \
			seek=128 obs=1K conv=notrunc

	#DPC
	dd if=${DEPLOY_DIR_IMAGE}/mc-utils/dpc.0x15-0x14.dtb of=${fw_blob} \
			seek=136 obs=1K conv=notrunc

	#MC
	dd if=${DEPLOY_DIR_IMAGE}/mc_app/mc.itb of=${fw_blob} \
			seek=144 obs=1K conv=notrunc

	# U-BOOT
	dd if=${DEPLOY_DIR_IMAGE}/atf/fip_fb-ls1048a-puma.bin of=${fw_blob} \
			seek=1688 obs=1K conv=notrunc

	# Install blob into image
	cp ${fw_blob} ${IMAGE_ROOTFS}/usr/share/u-boot/${fw_name}
}

build_firmware_image_fb_ls1048a_puma2 () {

	# Form a fw blob for fb-ls1048a-puma based board which can be programmed
	# on to the QSPI. This includes RCW, U-Boot binary, TF-A and DPAA
	# firmware images. The whole flash can be programmed by writing
	# resulting image at offset 0 in QSPI.
	# tg_mtdparts=4096K@0(allboot),128K@0(rcw),8K(dpl),8K(dpc),1536K(MC),8K(u-boot-env),1536K(u-boot)

	if [ ! -f ${DEPLOY_DIR_IMAGE}/atf/bl2_qspi_fb-ls1048a-puma2.pbl ]; then
		return
	fi

	fw_name=fb-ls1048a-puma2-qspi-fw-blob.bin
	fw_blob=${DEPLOY_DIR_IMAGE}/${fw_name}

	# Start from scratch, by filling whole image with 0xFFs
	dd if=/dev/zero bs=1K count=3224 | tr '\000' '\377' > ${fw_blob}

	# RCW
	dd if=${DEPLOY_DIR_IMAGE}/atf/bl2_qspi_fb-ls1048a-puma2.pbl \
			obs=1K seek=0 conv=notrunc of=${fw_blob}

	#DPL
	dd if=${DEPLOY_DIR_IMAGE}/mc-utils/dpl-eth.0x19-0x14.dtb of=${fw_blob} \
			seek=128 obs=1K conv=notrunc

	#DPC
	dd if=${DEPLOY_DIR_IMAGE}/mc-utils/dpc.0x19-0x14.dtb of=${fw_blob} \
			seek=136 obs=1K conv=notrunc

	#MC
	dd if=${DEPLOY_DIR_IMAGE}/mc_app/mc.itb of=${fw_blob} \
			seek=144 obs=1K conv=notrunc

	# U-BOOT
	dd if=${DEPLOY_DIR_IMAGE}/atf/fip_fb-ls1048a-puma2.bin of=${fw_blob} \
			seek=1688 obs=1K conv=notrunc

	# Install blob into image
	cp ${fw_blob} ${IMAGE_ROOTFS}/usr/share/u-boot/${fw_name}
}

build_firmware_image_fb_ls1048a_mbh () {

	# Form a fw blob for fb-ls1048a-puma based board which can be programmed
	# on to the QSPI. This includes RCW, U-Boot binary, TF-A and DPAA
	# firmware images. The whole flash can be programmed by writing
	# resulting image at offset 0 in QSPI.
	# tg_mtdparts=4096K@0(allboot),128K@0(rcw),8K(dpl),8K(dpc),1536K(MC),8K(u-boot-env),1536K(u-boot)

	if [ ! -f ${DEPLOY_DIR_IMAGE}/atf/bl2_qspi_fb-ls1048a-mbh.pbl ]; then
		return
	fi

	fw_name=fb-ls1048a-mbh-qspi-fw-blob.bin
	fw_blob=${DEPLOY_DIR_IMAGE}/${fw_name}

	# Start from scratch, by filling whole image with 0xFFs
	dd if=/dev/zero bs=1K count=3224 | tr '\000' '\377' > ${fw_blob}

	# RCW
	dd if=${DEPLOY_DIR_IMAGE}/atf/bl2_qspi_fb-ls1048a-mbh.pbl \
			obs=1K seek=0 conv=notrunc of=${fw_blob}

	#DPL
	dd if=${DEPLOY_DIR_IMAGE}/mc-utils/dpl-eth.mbh.0x15-0x13.dtb of=${fw_blob} \
			seek=128 obs=1K conv=notrunc

	#DPC
	dd if=${DEPLOY_DIR_IMAGE}/mc-utils/dpc.mbh.0x15-0x13.dtb of=${fw_blob} \
			seek=136 obs=1K conv=notrunc

	#MC
	dd if=${DEPLOY_DIR_IMAGE}/mc_app/mc.itb of=${fw_blob} \
			seek=144 obs=1K conv=notrunc

	# U-BOOT
	dd if=${DEPLOY_DIR_IMAGE}/atf/fip_fb-ls1048a-mbh.bin of=${fw_blob} \
			seek=1688 obs=1K conv=notrunc

	# Install blob into image
	cp ${fw_blob} ${IMAGE_ROOTFS}/usr/share/u-boot/${fw_name}
}

build_firmware_image_fb_ls1012_bobcat () {
	# Form a fw blob for ls1012atg based board which can be programmed
	# on to the QSPI. This includes RCW, U-Boot binary, PPA and PFE
	# firmware images. The whole flash can be programmed by writing
	# resulting image at offset 0 in QSPI.

	# Current layout is as follows:
	# tg_mtdparts=832K@0(allboot),64K@0(rcw),512K(u-boot),64k(pfe),192K(ppa),128K(u-boot-env),64K(boardinfo)

	if [ ! -f ${DEPLOY_DIR_IMAGE}/u-boot.bin-ls1012atg ]; then
		return
	fi

	fw_name=fb-ls1012-bobcat-qspi-fw-blob.bin
	fw_blob=${DEPLOY_DIR_IMAGE}/${fw_name}

	# Start from scratch, by filling whole image with 0xFFs
	dd if=/dev/zero bs=1K count=832 | tr '\000' '\377' > ${fw_blob}

	# RCW
	dd if=${DEPLOY_DIR_IMAGE}/rcw/ls1012atg/N_SSNP_9305/PBL_0x93_0x05_1000_250_1000_qspiboot.bin \
		of=${fw_blob} obs=1K conv=notrunc

	# U-BOOT
	dd if=${DEPLOY_DIR_IMAGE}/u-boot.bin-ls1012atg of=${fw_blob} \
		seek=64 obs=1K conv=notrunc

	# PFE firmware
	dd if=${DEPLOY_DIR_IMAGE}/engine-pfe-bin/pfe_fw_sbl.itb of=${fw_blob} \
		seek=576 obs=1K conv=notrunc

	# PPA
	dd if=${DEPLOY_DIR_IMAGE}/ppa-ls1012.itb of=${fw_blob} \
		seek=640 obs=1K conv=notrunc

	# Install blob into image
	cp ${fw_blob} ${IMAGE_ROOTFS}/usr/share/u-boot/${fw_name}
}
