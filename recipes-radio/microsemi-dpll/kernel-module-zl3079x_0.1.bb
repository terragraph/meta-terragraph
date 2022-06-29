SUMMARY = "Facebook Terragraph Kernel Module for Microsemi Network Synchronizer"
DESCRIPTION = "Facebook Terragraph Kernel Microsemi Network Synchronizer"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

inherit module kernel-extra-headers-export

SRC_URI = "file://src"
S = "${WORKDIR}/src"

DEPENDS = "kernel-module-terragraph-qca"
