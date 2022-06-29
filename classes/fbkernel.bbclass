# Extend the kernel class to cache kernel source

inherit kernel

#
# Update .config from defconfig if defconfig was changed since last
# build
do_configure_prepend() {
	if [ -f "${WORKDIR}/defconfig" ] && [  -f "${B}/.config" ]; then
		if ! cmp -s "${WORKDIR}/defconfig" "${B}/.config" ; then
			bbnote "Kernel config file ${WORKDIR}/defconfig changed"
			cp "${WORKDIR}/defconfig" "${B}/.config"
		fi
	fi
}
