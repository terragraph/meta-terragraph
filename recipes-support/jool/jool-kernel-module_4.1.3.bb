DESCRIPTION = "Jool is an Open Source IPv4/IPv6 Translator"
HOMEPAGE = "https://github.com/NICMx/Jool"
SECTION = "networking"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://../../COPYING;md5=b234ee4d69f5fce4486a80fdaf4a4263"

require jool.inc

MODULES_MODULE_SYMVERS_LOCATION = "common"

inherit module

S = "${WORKDIR}/jool-4.1.3/src/mod"

EXTRA_OEMAKE='KERNEL_DIR="${STAGING_KERNEL_BUILDDIR}"'

KERNEL_MODULE_AUTOLOAD_append += " \
  jool \
  jool_common \
  jool_siit \
"
