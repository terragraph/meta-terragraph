SUMMARY = "Facebook Terragraph Kernel Module for Ublox GPS"
DESCRIPTION = "Facebook Terragraph Kernel Module for Ublox GPS"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

inherit module kernel-extra-headers-export

SRC_URI = "file://src"
S = "${WORKDIR}/src"

KERNEL_MODULE_AUTOLOAD_append += "ublox-gps"

DEPENDS = "nl-driver-if-headers"
