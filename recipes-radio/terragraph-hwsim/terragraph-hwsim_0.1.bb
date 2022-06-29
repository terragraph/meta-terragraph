SUMMARY = "Facebook Terragraph Kernel Module for software simulation of Terragraph hardware"
DESCRIPTION = "Software simulator of Terragraph hardware"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

inherit module kernel-extra-headers-export

SRC_URI = "file://terragraph_hwsim"

S = "${WORKDIR}/terragraph_hwsim"

DEPENDS += " nl-driver-if-headers \
	     wireless-fw-interface"
