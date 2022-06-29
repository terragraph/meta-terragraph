inherit kernel-uboot
inherit kernel-fitimage

python __anonymous () {
    kerneltype = d.getVar('KERNEL_IMAGETYPE', True)
    if kerneltype == 'fitImage':
        depends = d.getVar("DEPENDS", True)
        depends = "%s u-boot-mkimage-native dtc-native" % depends
        d.setVar("DEPENDS", depends)

        # Override KERNEL_IMAGETYPE_FOR_MAKE variable, which is internal
        # to kernel.bbclass . We have to override it, since we pack Image
        # (at least for now) into the fitImage .
        d.setVar("KERNEL_IMAGETYPE_FOR_MAKE", "Image")

        image = d.getVar('INITRAMFS_IMAGE', True)
        if image:
            d.appendVarFlag('do_assemble_fitimage', 'depends', ' ${INITRAMFS_IMAGE}:do_image_complete')
}

#
# Base class has idiotic assumption that Image needs no compression
#
uboot_prep_kimage() {
	if test -e arch/${ARCH}/boot/compressed/vmlinux ; then
		vmlinux_path="arch/${ARCH}/boot/compressed/vmlinux"
		linux_suffix=""
		linux_comp="none"
	else
		vmlinux_path="vmlinux"
		linux_suffix=".gz"
		linux_comp="gzip"
	fi

	${OBJCOPY} -O binary -R .note -R .note.gnu.build-id -R .comment -S "${vmlinux_path}" linux.bin

	if [ "${linux_comp}" != "none" ] ; then
		gzip -9 linux.bin
		mv -f "linux.bin${linux_suffix}" linux.bin
	fi

	echo "${linux_comp}"
}

do_assemble_fitimage() {
	cd ${B}
	if test "x${KERNEL_IMAGETYPE}" = "xfitImage" ; then
		kernelcount=1
		dtbcount=""
		outfile="fit-image.its"
		rm -f fit-image.its

		fitimage_emit_fit_header ${outfile}

		#
		# Step 1: Prepare a kernel image section.
		#
		fitimage_emit_section_maint ${outfile} imagestart

		uboot_prep_kimage
		fitimage_emit_section_kernel ${outfile} "${kernelcount}" linux.bin "${linux_comp}"

		#
		# Step 2: Prepare a DTB image section
		#
		if test -n "${KERNEL_DEVICETREE}"; then
			dtbcount=1
			for DTB in ${KERNEL_DEVICETREE}; do
				if echo ${DTB} | grep -q '/dts/'; then
					bbwarn "${DTB} contains the full path to the the dts file, but only the dtb name should be used."
					DTB=`basename ${DTB} | sed 's,\.dts$,.dtb,g'`
				fi
				DTB_PATH="arch/${ARCH}/boot/dts/${DTB}"
				if [ ! -e "${DTB_PATH}" ]; then
					DTB_PATH="arch/${ARCH}/boot/${DTB}"
				fi

				fitimage_emit_section_dtb ${outfile} ${dtbcount} ${DTB_PATH}
				dtbcount=`expr ${dtbcount} + 1`
			done
		fi

		fitimage_emit_section_maint ${outfile} sectend

		# Force the first Kernel and DTB in the default config
		kernelcount=1
		dtbcount=1

		#
		# Step 3: Prepare a configurations section
		#
		fitimage_emit_section_maint ${outfile} confstart

		for DTB in ${KERNEL_DEVICETREE}; do
			echo "Emit fit config kernel: " ${kernelcount} " fit:" ${dtbcount}
			fitimage_emit_section_config ${outfile} ${kernelcount} ${dtbcount}
			dtbcount=`expr ${dtbcount} + 1`
		done

		fitimage_emit_section_maint ${outfile} sectend

		fitimage_emit_section_maint ${outfile} fitend

		#
		# Step 4: Assemble the image
		#
		uboot-mkimage -f fit-image.its arch/${ARCH}/boot/fitImage
	fi
}

