DESCRIPTION = "System Information Gatherer and Reporter"
HOMEPAGE = "https://github.com/hyperic/sigar"

LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=8f7bb094c7232b058c7e9f2e431f389c"

DEPENDS += "libtirpc"
RDEDENDS_${PN} += "libtirpc"

# v1.6.4
SRCREV = "ad47dc3b494e9293d1f087aebb099bdba832de5e"
SRC_URI = "git://github.com/hyperic/sigar.git;protocol=https;branch=master \
           file://0001-remove-unused-inline.patch \
           file://0002-Reimplement-adding-MemAvailable-for-3.14-and-fixing-.patch \
           file://0003-use-tirpc.patch \
           file://0004-Get-minor-major-from-sys-sysmacros.h-on-Linux.patch \
           "

inherit cmake

S = "${WORKDIR}/git"

# Remove --as-needed flag from linker command line. The sigar build system abuses
# LINK_FLAGS to specify libraries to link in, which places all of the -l<lib>
# directives in front of the objects, so all library dependencies are getting
# discarded as unsed, with the exception of glibc itself, courtesy of startup
# objects.
ASNEEDED = ""

# Force the .so into the main sigar package
FILES_${PN} = "/usr/lib/libsigar.so"

# The .so is added here automatically by the cmake config, reset
# the path to only include the header files
FILES_${PN}-dev = "/usr/include"

BBCLASSEXTEND = "native"
