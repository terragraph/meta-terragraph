SUMMARY = "Direct VPP interface"
DESCRIPTION = "Provide a fast kernel to user-land network interface"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

inherit module

SRC_URI = "file://direct-vpp"

S = "${WORKDIR}/direct-vpp"

do_install_append () {
	install -Dm0644 ${S}/dvpp_descriptor.h ${D}${includedir}/${BPN}/dvpp_descriptor.h
	install -Dm0644 ${S}/direct_vpp.h ${D}${includedir}/${BPN}/direct_vpp.h
	install -Dm0644 ${S}/dvpp_module_interface.h ${D}${includedir}/${BPN}/dvpp_module_interface.h
}

BBCLASSEXTEND = "native"

# We want the module loaded as early as possible, since
# in DPDK-less mode it may have to allocate HugePages
# so as to back up the network buffers, and this should be done
# before the memory has a change to get fragmented.
# TODO: once the feature isn't experimental anymore, enable
#       auto-load of direct-vpp
# KERNEL_MODULE_AUTOLOAD_append += "direct-vpp"
